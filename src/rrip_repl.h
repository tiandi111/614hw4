#ifndef RRIP_REPL_H_
#define RRIP_REPL_H_

#include "repl_policies.h"

// Static RRIP
class SRRIPReplPolicy : public ReplPolicy {
    protected:
        // add class member variables here
        uint32_t *array;
        bool *valid;
        uint32_t numLines;
        uint64_t rpvMax;

    public:
        // add member methods here, refer to repl_policies.h
        SRRIPReplPolicy(uint32_t _numLines, uint32_t _rpvMax) : numLines(_numLines), rpvMax(_rpvMax) {
            array = gm_calloc<uint32_t>(numLines);
            valid = gm_calloc<bool>(numLines);
            for(uint32_t i = 0; i < numLines; i++) {
                array[i] = 0;
                valid[i] = false;
            }
        }

        ~SRRIPReplPolicy() {
            gm_free(array);
            gm_free(valid);
        }

        void update(uint32_t id, const MemReq* req) {
            if(valid[id]) {
                array[id] = 0;
            } else {
                array[id] = rpvMax-1;
            }
        }

        virtual void replaced(uint32_t id) {
            valid[id] = false;
        }

        template <typename C> inline uint32_t rank(const MemReq* req, C cands) {
            uint32_t maxRRPV = -1;
            uint32_t maxId = -1;
            for (auto ci = cands.begin(); ci != cands.end(); ci.inc()) {
                bool validity = valid[*ci];
                uint32_t currRRPV = array[*ci];
                if(validity && (currRRPV > maxRRPV)) {
                    maxRRPV = currRRPV;
                    maxId = *ci;
                }
            }
            if(currRRPV < 0) panic("invalid rank call");
            uint32_t delta = rpvMax - maxRRPV;
            for(uint32_t i = 0; i < numLines; i++) {
                array[i] += delta;
            }
            return maxId;
        }

        DECL_RANK_BINDINGS;
};
#endif // RRIP_REPL_H_