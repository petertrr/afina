#include <afina/allocator/Pointer.h>

namespace Afina {
namespace Allocator {

Pointer::Pointer() : ptr(nullptr) {}
Pointer::Pointer(void** ptr) : ptr(ptr) {}
Pointer::Pointer(const Pointer &p) {ptr = p.ptr;}
Pointer::Pointer(Pointer &&p) {ptr = p.ptr; p.ptr = nullptr;}

Pointer &Pointer::operator=(const Pointer &p) { ptr = p.ptr; return *this; }
Pointer &Pointer::operator=(Pointer &&p) { ptr = p.ptr; p.ptr = nullptr; return *this; }

} // namespace Allocator
} // namespace Afina
