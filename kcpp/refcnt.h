#pragma once

// Intrusive shared_ptr, should be replaced with the actual shared_ptr I think.
template <typename T> class RefCnt
{
    T *ptr_;

    void addref() {
        if (ptr_) ptr_->addref();
    }

    explicit RefCnt(T* ptr): ptr_(ptr) {
        addref();
    }

    template <typename U>
    friend class RefCnt;
    template <typename U, typename... Args>
    friend RefCnt<U> make_refcnt(Args&&... args);
public:
    ~RefCnt() {
        if (ptr_) ptr_->release();
    }

    RefCnt(): ptr_(nullptr) {}
    RefCnt(nullptr_t): ptr_(nullptr) {}
    RefCnt(const RefCnt& other) {
        ptr_ = other.ptr_;
        addref();
    }
    template <typename U> RefCnt(const RefCnt<U>& other) {
        ptr_ = other.ptr_;
        addref();
    }
    RefCnt(RefCnt&& other): ptr_(latch(other.ptr_, nullptr)) {
    }
    template <typename U> RefCnt(RefCnt<U>&& other): ptr_(latch(other.ptr_, nullptr)) {
    }
    void operator=(RefCnt&& other) {
        ptr_ = latch(other.ptr_, nullptr);
    }
    void operator=(const RefCnt& other) {
        ptr_ = other.ptr_;
        addref();
    }
    template <typename U> void operator=(RefCnt<U>&& other) {
        ptr_ = latch(other.ptr_, nullptr);
    }

    void reset(nullptr_t p = nullptr) {
        if (ptr_) ptr_->release();
        ptr_ = nullptr;
    }

    // The "const T" returning variants should be accessible through
    // RefCnt<const T> (or at least that's how shared_ptr works).
    T* operator->() const {
        return ptr_;
    }
    T* get() const {
        return ptr_;
    }

    bool operator==(const RefCnt& other) const {
        return ptr_ == other.ptr_;
    }
    bool operator!=(const RefCnt& other) const {
        return ptr_ != other.ptr_;
    }
    template <typename U> bool operator==(const RefCnt<U>& other) const {
        return ptr_ == other.ptr_;
    }
    template <typename U> bool operator==(const U* rawptr) const {
        return ptr_ == rawptr;
    }
    bool operator==(nullptr_t) const {
        return ptr_ == nullptr;
    }
    bool operator!=(nullptr_t) const {
        return ptr_ != nullptr;
    }
    operator bool() const {
        return ptr_ != nullptr;
    }

    template<typename U> RefCnt<U> cast() const {
        return RefCnt<U>(static_cast<U*>(ptr_));
    }
    /**
     * Not entirely safe since the pointer needs to have come from RefCnt, or
     * at least the heap.
     *
     * The current implementation works with any heap pointer, but that's just
     * an accident. It'll be more shared_ptr:y eventually.
     */
    static RefCnt from_raw(T* p) {
        return RefCnt(p);
    }
};

template <typename T, typename... Args>
RefCnt<T> make_refcnt(Args&&... args) {
    return RefCnt(new T(static_cast<Args>(args)...));
}

template<typename T> const char* nameof; // = "<unknown>";

template <typename T>
class RefCounted
{
    uint32_t count = 0;

    void deleteme() {
        log(refcnt, "Deleting %s: %p\n", nameof<T>, this);
        delete static_cast<T*>(this);
    }

    template <typename U> friend class RefCnt;

    void addref() {
        count++;
        log(refcnt, "%s: %p++ => %u\n", nameof<T>, this, count);
    }
    void release() {
        log(refcnt, "%s: %p-- => %u\n", nameof<T>, this, count - 1);
        assert(count);
        if (!--count)
            deleteme();
    }

public:
    RefCounted() = default;
    RefCounted(const RefCounted&): RefCounted() {}
    RefCounted& operator =(const RefCounted&) { return this; }

    uint32_t get_refcount() const {
        return count;
    }
};
