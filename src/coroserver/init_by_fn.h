#ifndef SRC_COROSERVER_INIT_BY_FN_H_
#define SRC_COROSERVER_INIT_BY_FN_H_


namespace coroserver {

template<typename Fn>
class InitByFn {
public:

    using ValueType = decltype(std::declval<Fn>()());
    InitByFn(Fn &&fn):_fn(std::forward<Fn>(fn))  {}

    operator ValueType() const {
        return _fn();
    }

    operator ValueType() {
        return _fn();
    }

protected:
    Fn _fn;
};


}



#endif /* SRC_COROSERVER_INIT_BY_FN_H_ */
