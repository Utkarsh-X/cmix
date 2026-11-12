#ifndef INVARIANTS_H_
#define INVARIANTS_H_

namespace invariants {

inline int tail = 0; // 1 = article terminal section reached (See also, References, External links, Bibliography)

inline void Reset() {
    tail = 0;
}

inline unsigned int ApmExtra() {
    return (unsigned int)(tail & 1);
}

} // namespace invariants

#endif // INVARIANTS_H_
