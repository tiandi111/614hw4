//
// Created by 田地 on 2020/11/17.
//

#ifndef HAWKEYE_REPL_H
#define HAWKEYE_REPL_H

#include "repl_policies.h"
#include <iostream>
#include "hash.h"

enum OptResult {
    hit,
    miss,
    first
};

// data structures for OPTgen
class OPTgen {
protected:
    // sampled cache, use lru replacement policy, set-associative
    uint64_t *pcs;
    uint64_t *addrs;
    uint32_t setLen;
    uint32_t sampleCacheSize; // the size of the sample cache
    uint32_t mask;
    uint32_t setMask;
    // occupancy vector
    uint32_t *occVec;
    // cache sets
    uint32_t numSets;
    uint32_t ways;
    HashFamily* hf;
public:
    OPTgen() {}

    OPTgen(uint32_t _sets, uint32_t _ways, HashFamily* _hf) : numSets(_sets), ways(_ways), hf(_hf) {
        assert_msg(isPow2(numSets), "must have a power of 2 # sets, but you specified %d", numSets);
        setMask = numSets - 1;
        mask = 0x0000FFFF;
        setLen = 8 * ways;
        sampleCacheSize = numSets * setLen;
        // encountered memory problems when using gm_calloc and not time to solve it
        pcs = new uint64_t[sampleCacheSize];
        addrs = new uint64_t[sampleCacheSize];
        occVec = new uint32_t[sampleCacheSize];
//        pcs = gm_calloc<uint64_t>(sampleCacheSize);
//        addrs = gm_calloc<uint64_t>(sampleCacheSize);
//        occVec = gm_calloc<uint32_t>(sampleCacheSize);
        for(uint32_t i=0; i<sampleCacheSize; i++) {
            pcs[i] = 0;
            addrs[i] = 0;
            occVec[i] = 0;
        }
    }

    ~OPTgen() {
        // delete [] also raise errors, no time to fix
//        gm_free(pcs);
//        gm_free(addrs);
//        gm_free(occVec);
    }

    // predicts whether the given line will be a hit or a miss under OPT
    // true for hit, false for miss
    OptResult  predict(uint64_t lineAddr, uint64_t pc, uint64_t *lastPC) {
        uint32_t setid = hf->hash(0, lineAddr) & setMask;
        uint32_t first = setid * setLen;
        uint32_t last = first + setLen;
        // find the last empty entry
        while((last > first) && (addrs[last-1] <= 0)) { last--; }
        // find the last access
        uint32_t lastAccess = last;
        bool full = false, found = false;
        for(uint32_t i=0; i<(last-first); i++) {
            uint32_t idx = last-1-i;
            if(occVec[idx] >= ways) { // previous access has not been found and the cache is already full
                full = true;
            }
            if((addrs[idx] & mask) == (lineAddr & mask)) { // previous access found
                lastAccess = idx;
                *lastPC = pcs[idx];
                found = true;
                break;
            }
        }
        // increment occVec if hit
        OptResult result = found ? (full ? OptResult::miss : OptResult::hit) : OptResult::first;
        if(result == OptResult::hit) {
            for(uint32_t i = lastAccess; i < last; i++) { occVec[i]++; }
        }
        // if occupancy vector is full, shift left by 1
        if(last >= (first + setLen)) {
            for(uint32_t i = first; i < (first + setLen - 1); i++) {
                addrs[i] = addrs[i+1];
                pcs[i] = pcs[i+1];
                occVec[i] = occVec[i+1];
            }
            last = first + setLen - 1;
        }
        addrs[last] = lineAddr;
        pcs[last] = pc;
        occVec[last] = 0;
        return result;
    }

    bool findLastPC(uint64_t lineAddr, uint64_t *lastPC) {
        uint32_t setid = hf->hash(0, lineAddr) & setMask;
        uint32_t first = setid * setLen;
        uint32_t last = first + setLen;
        for (uint32_t i = last; i > first; i--) {
            if((addrs[i-1] & mask) == (lineAddr & mask)) {
                *lastPC = pcs[i-1];
                return true;
            }
        }
        return false;
    }
};

class HawkeyeReplPolicy : public ReplPolicy {
protected:
    uint32_t *predictor; // 3-bit counter predictor
    uint32_t *array; // rrip array
    uint64_t *cacheArray; // cache array
    uint32_t ways;
    uint32_t numLines;
    uint32_t predictorLen;
    uint32_t pcMask;
    uint32_t rpvMax;
    HashFamily* hf;
    OPTgen optGen;
public:
    HawkeyeReplPolicy(uint32_t _ways, uint32_t _numLines, uint8_t _pcIndexLen, HashFamily* _hf) :
            ways(_ways), numLines(_numLines), rpvMax(7), hf(_hf) {
        predictorLen = 1 << _pcIndexLen;
        pcMask = predictorLen - 1;

        predictor = gm_calloc<uint32_t>(predictorLen);
        array = gm_calloc<uint32_t>(numLines);
        cacheArray = gm_calloc<uint64_t>(numLines);

        optGen = OPTgen(64, ways, _hf);

        for(uint32_t i=0; i<numLines; i++) {
            array[i] = rpvMax+1;
            cacheArray[i] = 0;
        }
        for(uint32_t i=0; i<predictorLen; i++) {
            predictor[i] = 0;
        }
    }

    ~HawkeyeReplPolicy() {
        gm_free(array);
        gm_free(cacheArray);
        gm_free(predictor);
    }

    // updates on cache hit
    void update(uint32_t id, const MemReq *req) {
        // update access history and occupancy vector, generate OPT result
        uint64_t lastPC = 0;
        OptResult result = optGen.predict(req->lineAddr, req->pc, &lastPC);
        // train predictor for the last pc
        uint32_t idx = hf->hash(0, lastPC) & pcMask;
        if (result == OptResult::hit) {
            predictor[idx] += (predictor[idx] >= rpvMax) ? 0 : 1;
        } else if (result == OptResult::miss) {
            predictor[idx] -= (predictor[idx] <= 0) ? 0 : 1;
        }
        // update rrip
        bool fred = predictor[hf->hash(0, req->pc) & pcMask] > 3;
        array[id] = fred ? 0 : rpvMax;
    }

    // updates rrip on miss
    // since we don't pc here, all things need to be done here is moved into rank function below
    virtual void replaced(uint32_t id) {}

    // returns the replacement candidate
    template<typename C>
    inline uint32_t rank(const MemReq *req, C cands) {
        uint32_t idx = hf->hash(0, req->pc) & pcMask;
        // ranks
        uint32_t maxRRIP = array[*cands.begin()];
        uint32_t maxIdx = *cands.begin();
        for (auto ci = cands.begin(); ci != cands.end(); ci.inc()) {
            if (array[*ci] > maxRRIP) {
                maxRRIP = array[*ci];
                maxIdx = *ci;
            }
            // two cases:
            //      1. the entry is empty
            //      2. the line is cache-averse
            if (array[*ci] >= rpvMax) {
                break;
            }
        }
        // miss on cache-friendly line, ages all other lines
        bool fred = predictor[idx] > 3;
        if (fred) {
            for (auto ci = cands.begin(); ci != cands.end(); ci.inc()) {
                if(*ci != idx) {
                    array[*ci] += (array[*ci] >= (uint32_t)(rpvMax-1)) ? 0 : 1;
                }
            }
        }
        // detrains the predictor if a cache-friendly line is evicted
        if (array[maxIdx] < rpvMax) {
            uint64_t lastPC = 0;
            // if the evicted line is present in sampler, detrains the predictor
            bool found = optGen.findLastPC(cacheArray[maxIdx], &lastPC);
            if (found) {
                uint32_t lastPcIdx = hf->hash(0, lastPC) & pcMask;
                predictor[lastPcIdx] -= (predictor[lastPcIdx] <= 0) ? 0 : 1;
            }
        }
        // insert new line
        cacheArray[maxIdx] = req->lineAddr;
        return maxIdx;
    }

    DECL_RANK_BINDINGS;
};

#endif //INC_614HW4_HAWKEYE_REPL_H
