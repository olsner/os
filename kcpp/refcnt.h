template <typename T>
class RefCnt
{
    T *ptr_;
public:
    explicit RefCnt(T* ptr): ptr_(ptr) {
        if (ptr) ptr->addref();
    }
    ~RefCnt() {
        if (ptr_) ptr_->release();
    }

    RefCnt(): ptr_(nullptr) {}
    explicit RefCnt(nullptr_t): ptr_(nullptr) {}
    RefCnt(RefCnt&& other) {
        ptr_ = other.ptr_;
        other.ptr_ = nullptr;
    }
    void operator=(RefCnt&& other) {
        ptr_ = other.ptr_;
        other.ptr_ = nullptr;
    }
    void operator=(const RefCnt& other) {
        ptr_ = other.ptr_;
        if (ptr_) ptr_->addref();
    }

    void reset(nullptr_t p = nullptr) {
        if (ptr_) ptr_->release();
        ptr_ = nullptr;
    }
    void reset_addref(T* ptr) {
        if (ptr_) ptr_->release();
        ptr_ = ptr;
        if (ptr_) ptr_->addref();
    }
    void reset_take(T* ptr) {
        if (ptr_) ptr_->release();
        ptr_ = ptr;
    }

    T* operator->() {
        return ptr_;
    }
    const T* operator ->() const {
        return ptr_;
    }
    T* get() {
        return ptr_;
    }
};

template <typename T>
class RefCounted
{
    uint32_t count;

    void deleteme() {
        delete static_cast<T*>(this);
    }
public:
    RefCounted(): count(0) {}
    RefCounted(const RefCounted&) = delete;
    RefCounted& operator =(const RefCounted&) = delete;

    void addref() {
        assert(count < UINT32_MAX);
        count++;
    }
    void release() {
        assert(count);
        if (!--count)
            deleteme();
    }

    uint32_t get_refcount() const {
        return count;
    }
};
