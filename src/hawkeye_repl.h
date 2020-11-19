//
// Created by 田地 on 2020/11/17.
//

#ifndef HAWKEYE_REPL_H
#define HAWKEYE_REPL_H

class HawkeyeReplPolicy : public ReplPolicy {
    protected:
        // hawkeye predictor
        uint8_t *predictor;
        // array for cache rrip value
        uint8_t *array;
        //
        uint32_t ways;
        uint32_t numLines;
        uint32_t predictorLen;
        uint32_t pcMask;
    public:
        HawkeyeReplPolicy(uint32_t _ways, uint32_t _numLines, uint8_t _pcIndexLen) :
        ways(_ways), numLines(_numLines) {
            predictorLen = 1 << _pcIndexLen;
            pcMask = ~0 >> (32-_pcIndexLen);
            array = gm_calloc<uint8_t>(numLines);
            predictor = gm_calloc<uint8_t>(predictorLen);
        }

        ~HawkeyeReplPolicy() {
            gm_free(array);
            gm_free(predictor);
        }

        // updates on cache hit/miss
        void update(uint32_t id, const MemReq* req) {
            // TODO: 1. update hist and occuVec
            //   2. update predictor
            //   3. update rrip value, notice that cache hit and miss should be differentiated
        }
        // replaces cache line
        virtual void replaced(uint32_t id) {
            // TODO: 1. update 
        }

        template <typename C> inline uint32_t rank(const MemReq* req, C cands) {
            // TODO: rank
        }

        void train(uint32_t pc, bool hit) {

        }

        DECL_RANK_BINDINGS;

        // data structures for OPTgen
        class OPTgen {
            protected:
                // sampled cache, use lru replacement policy
                uint32_t *pc;
                uint32_t *addr;
                uint32_t *cycles;
                uint32_t sampleCacheSize; // the size of the sample cache
                uint32_t sets; // number of sets of the sampled cache
                uint32_t lru; // point to the least recently used position
                uint32_t mru; //
                uint32_t mask;
                // occupancy vector
                // TODO: occupancy vector need to be a circular array
                uint32_t *occVec;
                uint32_t vecLen; // the length of the occupancy vector

        public:
                OPTgen(uint32_t _sets, uint32_t _ways, uint32_t _timeQuantum, float sampleRate) : sets(_sets) {
                    mask = (~0) >> 16;
                    lru = 0;
                    mru = 0;
                    sampleCacheSize = _sets * _ways * 8 * sampleRate;
                    vecLen = _sets * _ways / _timeQuantum;
                    pc = gm_calloc<uint32_t>(sampleCacheSize);
                    addr = gm_calloc<uint32_t>(sampleCacheSize);
                    cycles = gm_calloc<uint32_t>(sampleCacheSize);
                    occVec = gm_calloc<uint32_t>(vecLen);
                }

                ~OPTgen() {
                    gm_free(pc);
                    gm_free(addr);
                    gm_free(cycles);
                    gm_free(occVec);
                }

                // predicts whether the given line will be a hit or a miss under OPT
                // true for hit, false for miss
                bool predict(uint32_t lineAddr, uint32_t pc, uint32_t cycle) {
                    // find the most recent access history
                    for(uint32_t i=0; i<sampleCacheSize; i++) {
                        uint32_t idx = i<=mru? mru-i : sampleCacheSize + (mru-i) % sampleCacheSize;
                        if((addr[idx] & mask) == (lineAddr & mask)) {
                            for()
                        }
                    }
                    // insert access history
                    if(((mru-lru)==sampleCacheSize) || (mru==(lru-1))) {

                    } else {

                    }
                }
        };
};

#endif //INC_614HW4_HAWKEYE_REPL_H
