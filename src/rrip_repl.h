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
            array[id] = array[id] == rpvMax+1? rpvMax-1 : 0;
        }

        virtual void replaced(uint32_t id) {
            array[id] = rpvMax+1;
        }

        template <typename C> inline uint32_t rank(const MemReq* req, C cands) {
            while(true) {
                for (auto ci = cands.begin(); ci != cands.end(); ci.inc()) {
                    if(array[*ci] == rpvMax ) {
                        return *ci;
                    }
                }
                for(uint32_t i = 0; i < numLines; i++) {
                    array[i] ++;
                }
            }
        }

        DECL_RANK_BINDINGS;
};
#endif // RRIP_REPL_H_