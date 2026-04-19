#ifndef IGNORE_UNUSED_UTIL_HPP
#define IGNORE_UNUSED_UTIL_HPP


template<typename... Args>
void ignore_unused(Args&&...) {
    static_cast<void>(sizeof...(Args));
}

#endif //IGNORE_UNUSED_UTIL_HPP