//
//  make_tuple_indices.hpp
//  fibio
//
//  Created by Chen Xu on 14-3-21.
//  Copyright (c) 2014 0d0a.com. All rights reserved.
//

#ifndef fibio_fibers_detail_make_tuple_indices_hpp
#define fibio_fibers_detail_make_tuple_indices_hpp

#include <memory>
#include <boost/static_assert.hpp>

namespace fibio { namespace fibers { namespace detail {
    // make_tuple_indices
    template <std::size_t...> struct tuple_indices
    {};
    
    template <std::size_t Sp, class IntTuple, std::size_t Ep>
    struct make_indices_imp;
    
    template <std::size_t Sp, std::size_t... Indices, std::size_t Ep>
    struct make_indices_imp<Sp, tuple_indices<Indices...>, Ep>
    {
        typedef typename make_indices_imp<Sp+1, tuple_indices<Indices..., Sp>, Ep>::type type;
    };
    
    template <std::size_t Ep, std::size_t... Indices>
    struct make_indices_imp<Ep, tuple_indices<Indices...>, Ep>
    {
        typedef tuple_indices<Indices...> type;
    };
    
    template <std::size_t Ep, std::size_t Sp = 0>
    struct make_tuple_indices
    {
        BOOST_STATIC_ASSERT_MSG(Sp <= Ep, "make_tuple_indices input error");
        typedef typename make_indices_imp<Sp, tuple_indices<>, Ep>::type type;
    };
    
    // invoke
    template <class Fp, class A0, class... Args>
    inline auto invoke(Fp&& f, A0&& a0, Args&&... args)
    -> decltype((std::forward<A0>(a0).*f)(std::forward<Args>(args)...))
    { return (std::forward<A0>(a0).*f)(std::forward<Args>(args)...); }
    
    template <class Fp, class A0, class... Args>
    inline auto invoke(Fp&& f, A0&& a0, Args&&... args)
    -> decltype(((*std::forward<A0>(a0)).*f)(std::forward<Args>(args)...))
    { return ((*std::forward<A0>(a0)).*f)(std::forward<Args>(args)...); }
    
    template <class Fp, class A0>
    inline auto invoke(Fp&& f, A0&& a0)
    -> decltype(std::forward<A0>(a0).*f)
    { return std::forward<A0>(a0).*f; }
    
    template <class Fp, class A0>
    inline auto invoke(Fp&& f, A0&& a0)
    -> decltype((*std::forward<A0>(a0)).*f)
    { return (*std::forward<A0>(a0)).*f; }
    
    template <class Fp, class... Args>
    inline auto invoke(Fp&& f, Args&&... args)
    -> decltype(std::forward<Fp>(f)(std::forward<Args>(args)...))
    { return std::forward<Fp>(f)(std::forward<Args>(args)...); }

    /// decay_copy
    template <class T>
    typename std::decay<T>::type decay_copy(T&& t)
    { return std::forward<T>(t); }

    /// fiber_data_base
    struct fiber_data_base
    {
        virtual ~fiber_data_base(){}
        virtual void run()=0;
    };
    
    /// fiber_data
    template<typename F, class... ArgTypes>
    class fiber_data : public fiber_data_base
    {
    public:
        fiber_data(F&& f_, ArgTypes&&... args_)
        : fp(std::forward<F>(f_), std::forward<ArgTypes>(args_)...)
        {}

        template <std::size_t... Indices>
        void run2(tuple_indices<Indices...>)
        { invoke(std::move(std::get<0>(fp)), std::move(std::get<Indices>(fp))...); }

        void run()
        {
            typedef typename make_tuple_indices<std::tuple_size<std::tuple<F, ArgTypes...> >::value, 1>::type index_type;
            run2(index_type());
        }
        
    private:
        /// Non-copyable
        fiber_data(const fiber_data&)=delete;
        void operator=(const fiber_data&)=delete;
        std::tuple<typename std::decay<F>::type, typename std::decay<ArgTypes>::type...> fp;
    };
    
    /// make_fiber_data
    /**
     * wrap entry function and arguments into a tuple
     */
    template<typename F, class... ArgTypes>
    inline fiber_data_base *make_fiber_data(F&& f, ArgTypes&&... args)
    {
        return new fiber_data<typename std::remove_reference<F>::type, ArgTypes...>(std::forward<F>(f),
                                                                                    std::forward<ArgTypes>(args)...);
    }
}}} // End of namespace fibio::fibers::detail
#endif
