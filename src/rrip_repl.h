#ifndef RRIP_REPL_H_
#define RRIP_REPL_H_

#include "repl_policies.h"

// Static RRIP
class SRRIPReplPolicy : public ReplPolicy {
    protected:
        // add class member variables here
        uint32_t *array;
        uint32_t numLines;
        uint64_t rpvMax;

    public:
        // add member methods here, refer to repl_policies.h
        SRRIPReplPolicy(uint32_t _numLines, uint32_t _rpvMax) : numLines(_numLines), rpvMax(_rpvMax) {
            array = gm_calloc<uint32_t>(numLines);
            for(uint32_t i = 0; i < numLines; i++) {
                array[i] = rpvMax+1;
            }
        }

        ~SRRIPReplPolicy() {
            gm_free(array);
        }

        void update(uint32_t id, const MemReq* req) {
            array[id] = array[id] == rpvMax+1 ? rpvMax-1 : 0;
        }

        virtual void replaced(uint32_t id) {
            array[id] = rpvMax+1;
        }

        template <typename C> inline uint32_t rank(const MemReq* req, C cands) {
            uint32_t maxRRPV = -1;
            uint32_t maxId = -1;
            for (auto ci = cands.begin(); ci != cands.end(); ci.inc()) {
                uint32_t currRRPV = array[*ci];
                if(currRRPV == rpvMax+1) {
                    return *ci;
                }
                if(currRRPV > maxRRPV) {
                    maxRRPV = currRRPV;
                    maxId = *ci;
                }
            }
            uint32_t delta = rpvMax-maxRRPV;
            for(uint32_t i = 0; i < numLines; i++) {
                if(array[i] <= rpvMax) {
                    array[i] += delta;
                }
            }
            return maxId;
        }

        DECL_RANK_BINDINGS;
};
#endif // RRIP_REPL_H_