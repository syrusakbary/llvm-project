#include "__threading_support"

_LIBCPP_BEGIN_NAMESPACE_STD

int __libcpp_recursive_mutex_init(__libcpp_recursive_mutex_t *__m) { return 0; }
int __libcpp_recursive_mutex_lock(__libcpp_recursive_mutex_t *__m) { return 0; }
bool __libcpp_recursive_mutex_trylock(__libcpp_recursive_mutex_t *__m) { return false; }
int __libcpp_recursive_mutex_unlock(__libcpp_recursive_mutex_t *__m) { return 0; }
int __libcpp_recursive_mutex_destroy(__libcpp_recursive_mutex_t *__m) { return 0; }
int __libcpp_mutex_lock(__libcpp_mutex_t *__m) { return 0; }
bool __libcpp_mutex_trylock(__libcpp_mutex_t *__m) { return false; }
int __libcpp_mutex_unlock(__libcpp_mutex_t *__m) { return 0; }
int __libcpp_mutex_destroy(__libcpp_mutex_t *__m) { return 0; }
int __libcpp_condvar_signal(__libcpp_condvar_t* __cv) { return 0; }
int __libcpp_condvar_broadcast(__libcpp_condvar_t* __cv) { return 0; }
int __libcpp_condvar_wait(__libcpp_condvar_t* __cv, __libcpp_mutex_t* __m) { return 0; }
int __libcpp_condvar_timedwait(__libcpp_condvar_t *__cv, __libcpp_mutex_t *__m, timespec *__ts) { return 0; }
int __libcpp_condvar_destroy(__libcpp_condvar_t* __cv) { return 0; }
int __libcpp_execute_once(__libcpp_exec_once_flag *flag, void (*init_routine)(void)) { return 0; }
bool __libcpp_thread_id_equal(__libcpp_thread_id t1, __libcpp_thread_id t2) { return false; }
bool __libcpp_thread_id_less(__libcpp_thread_id t1, __libcpp_thread_id t2) { return false; }
bool __libcpp_thread_isnull(const __libcpp_thread_t *__t) { return false; }
int __libcpp_thread_create(__libcpp_thread_t *__t, void *(*__func)(void *), void *__arg) { return 0; }
__libcpp_thread_id __libcpp_thread_get_current_id() { return 0; }
__libcpp_thread_id __libcpp_thread_get_id(const __libcpp_thread_t *__t) { return 0; }
int __libcpp_thread_join(__libcpp_thread_t *__t) { return 0; }
int __libcpp_thread_detach(__libcpp_thread_t *__t) { return 0; }
void __libcpp_thread_yield() {}
void __libcpp_thread_sleep_for(const std::chrono::nanoseconds& __ns) {}
int __libcpp_tls_create(__libcpp_tls_key* __key, void(_LIBCPP_TLS_DESTRUCTOR_CC* __at_exit)(void*)) { return 0; }
void *__libcpp_tls_get(__libcpp_tls_key __key) { return 0; }
int __libcpp_tls_set(__libcpp_tls_key __key, void *__p) { return 0; }

_LIBCPP_END_NAMESPACE_STD
