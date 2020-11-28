//
// Created by 田地 on 2020/11/17.
//

#ifndef HAWKEYE_REPL_H
#define HAWKEYE_REPL_H

#include "repl_policies.h"

enum OptResult {
    hit,
    miss,
    first
};

// data structures for OPTgen
class OPTgen {
protected:
    // sampled cache, use lru replacement policy, set-associative
    uint32_t *pcs;
    uint32_t *addrs;
    uint32_t setLen;
    uint32_t sampleCacheSize; // the size of the sample cache
    uint32_t mask;
    // occupancy vector
    uint32_t *occVec;
    // cache sets
    uint32_t numSets;
public:
    OPTgen() {}

    OPTgen(uint32_t _sets, uint32_t _ways, uint32_t _timeQuantum) : numSets(_sets) {
        mask = 0x0000FFFF;
        setLen = 8 * _ways;
        sampleCacheSize = numSets * setLen;
        pcs = gm_calloc<uint32_t>(sampleCacheSize);
        addrs = gm_calloc<uint32_t>(sampleCacheSize);
        occVec = gm_calloc<uint32_t>(sampleCacheSize);
    }

    ~OPTgen() {
        gm_free(pcs);
        gm_free(addrs);
        gm_free(occVec);
    }

    // predicts whether the given line will be a hit or a miss under OPT
    // true for hit, false for miss
    OptResult predict(uint32_t lineAddr, uint32_t pc, uint32_t cycle, uint32_t *lastPC) {
        uint32_t setid = lineAddr % numSets;
        uint32_t first = setid * setLen;
        uint32_t last = first + setLen;
        // find the last empty entry
        while((last > first) && (addrs[last-1] < 0)) { last--; }
        // find the last access
        uint32_t lastAccess = last;
        bool full = false, found = false;
        for(uint32_t i=last-1; i>=first; i--) {
            if(occVec[i] >= setLen) { // previous access has not been found and the cache is already full
                full = true;
            }
            if(addrs[i] == (lineAddr & mask)) { // previous access found and the cache is not full, hit
                lastAccess = i;
                *lastPC = pcs[i];
                found = true;
                break;
            }
        }
        // increment occVec if hit
        OptResult result = found ? (full ? OptResult::miss : OptResult::hit) : OptResult::first;
        if(result == hit) {
            for(uint32_t i = lastAccess; i < last; i++) { occVec[i]++; }
        }
        // if occupancy vector is full, shift left by 1
        if(last >= (first + setLen)) {
            for(uint32_t i = first; i < (first + setLen - 1); i++) {
                addrs[i] = addrs[i+1];
                pcs[i] = pcs[i+1];
                occVec[i] = occVec[i+1];
            }
        }
        addrs[last-1] = lineAddr & mask;
        pcs[last-1] = pc & mask;
        occVec[last-1] = 0;
        return result;
    }

    bool findLastPC(uint32_t lineAddr, uint32_t *lastPC) {
        uint32_t setid = lineAddr % numSets;
        uint32_t first = setid * setLen;
        uint32_t last = first + setLen;
        for (uint32_t i = last-1; i >=first; i--) {
            if(addrs[i] == (lineAddr & mask)) {
                *lastPC = pcs[i];
                return true;
            }
        }
        return false;
    }
};

class HawkeyeReplPolicy : public ReplPolicy {
protected:
    uint8_t *predictor; // 3-bit counter predictor
    uint8_t *array; // rrip array
    uint32_t *cacheArray; // cache array
    uint32_t ways;
    uint32_t numLines;
    uint32_t predictorLen;
    uint32_t pcMask;
    uint8_t rpvMax;
    OPTgen optGen;
public:
    HawkeyeReplPolicy(uint32_t _ways, uint32_t _numLines, uint8_t _pcIndexLen) :
            ways(_ways), numLines(_numLines), rpvMax(7) {
        predictorLen = 1 << _pcIndexLen;
        pcMask = predictorLen - 1; // todo
        predictor = gm_calloc<uint8_t>(predictorLen);
        array = gm_calloc<uint8_t>(numLines);
        cacheArray = gm_calloc<uint32_t>(numLines);
        optGen = OPTgen(64, ways, 1);
    }

    ~HawkeyeReplPolicy() {
        gm_free(array);
        gm_free(cacheArray);
        gm_free(predictor);
    }

    // updates on cache hit
    void update(uint32_t id, const MemReq *req) {
        // update access history and occupancy vector, generate OPT result
        uint32_t lastPC;
        OptResult result = optGen.predict(req->lineAddr, req->pc, req->cycle, &lastPC);
        // train predictor for the last pc
        uint32_t idx = lastPC & pcMask;
        if (result == hit) {
            predictor[idx] += (predictor[idx] == rpvMax) ? 0 : 1;
        } else if (result == miss) {
            predictor[idx] -= (predictor[idx] == 0) ? 0 : 1;
        }
        // update rrip
        bool fred = predictor[req->pc & pcMask] > 3;
        array[id] = fred ? rpvMax : 0;
    }

    // updates rrip on miss
    // since we don't pc here, all things need to be done here is moved into rank function below
    virtual void replaced(uint32_t id) {}

    // returns the replacement candidate
    template<typename C>
    inline uint32_t rank(const MemReq *req, C cands) {
        uint32_t idx = req->pc & pcMask;
        // ranks
        uint8_t maxRRIP = array[0];
        uint32_t maxIdx = 0;
        for (auto ci = cands.begin(); ci != cands.end(); ci.inc()) {
            if (array[*ci] > maxRRIP) {
                maxRRIP = array[*ci];
                maxIdx = *ci;
            }
            // two situations:
            //      1. the entry is empty
            //      2. the line is cache-averse
            if (array[*ci] >= rpvMax) {
                break;
            }
        }
        // miss on cache-friendly line, ages all other lines
        if (predictor[idx] > 3) {
            for (auto ci = cands.begin(); ci != cands.end(); ci.inc()) {
                array[*ci] += array[*ci] >= rpvMax ? 0 : 1;
            }
        }
        // detrains the predictor if eviction happens
        if (array[maxIdx] <= rpvMax) {
            uint32_t lastPC;
            // if the evicted line is present in sampler, detrains the predictor
            bool found = optGen.findLastPC(cacheArray[maxIdx], &lastPC);
            if (found) {
                uint32_t lastPcIdx = lastPC & pcMask;
                predictor[lastPcIdx] -= (predictor[lastPcIdx] == 0) ? 0 : 1;
            }
        }
        // insert new line
        cacheArray[maxIdx] = req->lineAddr;
        return maxIdx;
    }

    DECL_RANK_BINDINGS;
};

#endif //INC_614HW4_HAWKEYE_REPL_H
