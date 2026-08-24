module;

#include <cstddef>
#include <exception>

export module forge.vm.wasm.interpret.guarded_ptr;

export import forge.vm.wasm.interpret.exceptions;
export import forge.vm.wasm.interpret.scope_guard;

export namespace forge::vm::wasm::interpret {
template <typename T> struct guarded_ptr {
   T* raw_ptr;
   T* orig_ptr;
   T* bnds;
   guarded_ptr(T* rp, size_t bnds) : raw_ptr(rp), orig_ptr(rp), bnds(rp + bnds) {}

   inline guarded_ptr& operator+=(size_t i) {
      detail::check<exceptions::pointer_out_of_bounds>((i <= static_cast<std::size_t>(bnds - raw_ptr)),
                                                       "overbounding pointer");
      raw_ptr += i;
      return *this;
   }

   inline guarded_ptr& operator++() {
      detail::check<exceptions::pointer_out_of_bounds>((raw_ptr < bnds), "overbounding pointer");
      raw_ptr += 1;
      return *this;
   }

   inline guarded_ptr operator++(int) {
      guarded_ptr tmp = *this;
      ++*this;
      return tmp;
   }

   friend inline guarded_ptr operator+(const guarded_ptr& arg, size_t i) {
      guarded_ptr tmp = arg;
      tmp += i;
      return tmp;
   }
   friend inline guarded_ptr operator+(std::size_t i, const guarded_ptr& arg) {
      guarded_ptr tmp = arg;
      tmp += i;
      return tmp;
   }

   inline T& operator*() const {
      detail::check<exceptions::pointer_out_of_bounds>((raw_ptr < bnds), "accessing out of bounds");
      return *raw_ptr;
   }

   inline T* operator->() const {
      detail::check<exceptions::pointer_out_of_bounds>((raw_ptr < bnds), "accessing out of bounds");
      return raw_ptr;
   }

   T& operator=(const guarded_ptr<T>& ptr) = delete;

   inline T* raw() {
      return raw_ptr;
   }

   inline size_t offset() {
      return raw_ptr - orig_ptr;
   }

   // reduces the bounds for the lifetime of the returned object
   auto scoped_shrink_bounds(std::size_t n) {
      detail::check<exceptions::pointer_out_of_bounds>((n <= static_cast<std::size_t>(bnds - raw_ptr)),
                                                       "guarded ptr out of bounds");
      T* old_bnds = bnds;
      bnds = raw_ptr + n;
      return scope_guard{[this, old_bnds]() { bnds = old_bnds; }};
   }
   // verifies that the pointer is advanced by exactly n before
   // the returned object is destroyed.
   auto scoped_consume_items(std::size_t n) {
      detail::check<exceptions::pointer_out_of_bounds>((n <= static_cast<std::size_t>(bnds - raw_ptr)),
                                                       "guarded ptr out of bounds");
      int exceptions = std::uncaught_exceptions();
      T* old_bnds = bnds;
      bnds = raw_ptr + n;
      struct throwing_destructor {
         ~throwing_destructor() noexcept(false) {}
      };
      throwing_destructor x;
      return scope_guard{[this, old_bnds, exceptions, x]() {
         detail::check<exceptions::pointer_out_of_bounds>((exceptions != std::uncaught_exceptions() || raw_ptr == bnds),
                                                          "guarded_ptr not advanced");
         bnds = old_bnds;
      }};
   }

   inline size_t bounds() {
      return bnds - orig_ptr;
   }

   inline T at(size_t index) const {
      detail::check<exceptions::pointer_out_of_bounds>((index < static_cast<std::size_t>(bnds - raw_ptr)),
                                                       "accessing out of bounds");
      return raw_ptr[index];
   }

   inline T at() const {
      detail::check<exceptions::pointer_out_of_bounds>((raw_ptr < bnds), "accessing out of bounds");
      return *raw_ptr;
   }

   inline T operator[](size_t index) const {
      return at(index);
   }
};
} // namespace forge::vm::wasm::interpret
