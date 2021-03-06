//
//  cv_object.cpp
//  fibio
//
//  Created by Chen Xu on 14-3-4.
//  Copyright (c) 2014 0d0a.com. All rights reserved.
//

#include "cv_object.hpp"
#include <boost/system/error_code.hpp>

namespace fibio { namespace fibers { namespace detail {
    struct fibio_relock_guard {
        inline fibio_relock_guard(mutex_object *mtx, fiber_ptr_t f)
        : mtx_(mtx)
        , this_fiber(f)
        { mtx_->unlock(this_fiber); }
        
        inline ~fibio_relock_guard()
        { mtx_->lock(this_fiber); }
        
        mutex_object *mtx_;
        fiber_ptr_t this_fiber;
    };
    
    void condition_variable_object::wait(mutex_object *m, fiber_ptr_t this_fiber) {
        CHECK_CALLER(this_fiber);
        if (this_fiber!=m->owner_) {
            // This fiber doesn't own the mutex
            BOOST_THROW_EXCEPTION(condition_error(boost::system::errc::operation_not_permitted));
        }
        {
            std::lock_guard<spinlock> lock(mtx_);
            // The "suspension of this fiber" is actually happened here, not the pause()
            // as other will see there is a fiber in the waiting queue.
            suspended_.push_back(suspended_item({m, this_fiber, 0}));
        }
        fibio_relock_guard lk(m, this_fiber);
        this_fiber->pause();
    }
    
    static inline void timeout_handler(fiber_ptr_t this_fiber,
                                condition_variable_object *this_cv,
                                timer_t *t,
                                cv_status &ret,
                                boost::system::error_code ec)
    {
        if(!ec) {
            // Timeout
            // Timeout handler, find and remove this fiber from waiting queue
            std::lock_guard<spinlock> lock(this_cv->mtx_);
            ret=cv_status::timeout;
            // Find and remove this fiber from waiting queue
            auto i=std::find_if(this_cv->suspended_.begin(),
                                this_cv->suspended_.end(),
                                std::bind(is_this_fiber<condition_variable_object::suspended_item>,
                                          this_fiber,
                                          std::placeholders::_1)
                                );
            if (i!=this_cv->suspended_.end()) {
                this_cv->suspended_.erase(i);
            }
        }
        this_fiber->resume();
    }
    
    cv_status condition_variable_object::wait_usec(mutex_object *m, fiber_ptr_t this_fiber, uint64_t usec) {
        //CHECK_CALLER(this_fiber);
        cv_status ret=cv_status::no_timeout;
        if (this_fiber!=m->owner_) {
            // This fiber doesn't own the mutex
            BOOST_THROW_EXCEPTION(condition_error(boost::system::errc::operation_not_permitted));
        }
        timer_t t(this_fiber->get_io_service());
        {
            std::lock_guard<spinlock> lock(mtx_);
            suspended_.push_back(suspended_item({m, this_fiber, &t}));
            t.expires_from_now(std::chrono::microseconds(usec));
            t.async_wait(this_fiber->get_fiber_strand().wrap(std::bind(timeout_handler,
                                                                       this_fiber,
                                                                       this,
                                                                       &t,
                                                                       std::ref(ret),
                                                                       std::placeholders::_1)));
        }
        fibio_relock_guard lk(m, this_fiber);
        this_fiber->pause();
        return ret;
    }

    void condition_variable_object::notify_one() {
        {
            std::lock_guard<spinlock> lock(mtx_);
            if (suspended_.empty()) {
                return;
            }
            suspended_item p(suspended_.front());
            suspended_.pop_front();
            if (p.t_) {
                // Cancel attached timer if it's set
                // Timer handler will reschedule the waiting fiber
                p.t_->cancel();
                p.t_=0;
            } else {
                // No timer attached to the waiting fiber, directly schedule it
                p.f_->resume();
            }
        }
        // Only yield if currently in a fiber
        // CV can be used to notify a fiber from not-a-fiber, i.e. foreign thread
        if (fiber_object::current_fiber_) {
            fiber_object::current_fiber_->yield();
        }
    }
    
    void condition_variable_object::notify_all() {
        {
            std::lock_guard<spinlock> lock(mtx_);
            while (!suspended_.empty()) {
                suspended_item p(suspended_.front());
                suspended_.pop_front();
                if (p.t_) {
                    // Cancel attached timer if it's set
                    // Timer handler will reschedule the waiting fiber
                    p.t_->cancel();
                } else {
                    // No timer attached to the waiting fiber, directly schedule it
                    p.f_->resume();
                }
            }
        }
        // Only yield if currently in a fiber
        // CV can be used to notify a fiber from not-a-fiber, i.e. foreign thread
        if (fiber_object::current_fiber_) {
            fiber_object::current_fiber_->yield();
        }
    }
}}} // End of namespace fibio::fibers::detail

namespace fibio { namespace fibers {
    condition_variable::condition_variable()
    : impl_(new detail::condition_variable_object)
    {}
    
    void condition_variable::wait(std::unique_lock<mutex>& lock) {
        CHECK_CURRENT_FIBER;
        if (detail::fiber_object::current_fiber_) {
            impl_->wait(lock.mutex()->impl_.get(), detail::fiber_object::current_fiber_->shared_from_this());
        }
    }
    
    cv_status condition_variable::wait_usec(std::unique_lock<mutex>& lock, uint64_t usec) {
        CHECK_CURRENT_FIBER;
        if (detail::fiber_object::current_fiber_) {
            return impl_->wait_usec(lock.mutex()->impl_.get(), detail::fiber_object::current_fiber_->shared_from_this(), usec);
        }
        return cv_status::timeout;
    }
    
    void condition_variable::notify_one() {
        impl_->notify_one();
    }
    
    void condition_variable::notify_all() {
        impl_->notify_all();
    }
    
    void condition_variable::impl_deleter::operator()(detail::condition_variable_object *p) {
        delete p;
    }
    
    struct cleanup_handler {
        condition_variable &c_;
        std::unique_lock<mutex> l_;
        
        cleanup_handler(condition_variable &cond,
                        std::unique_lock<mutex> lk)
        : c_(cond)
        , l_(std::move(lk))
        {}
        
        void operator()() {
            l_.unlock();
            c_.notify_all();
        }
    };
    
    inline void run_cleanup_handler(cleanup_handler *h) {
        std::unique_ptr<cleanup_handler> ch(h);
        (*ch)();
    }
    
    void notify_all_at_fiber_exit(condition_variable &cond,
                                   std::unique_lock<mutex> lk)
    {
        CHECK_CURRENT_FIBER;
        if (detail::fiber_object::current_fiber_) {
            cleanup_handler *h=new cleanup_handler(cond, std::move(lk));
            detail::fiber_object::current_fiber_->add_cleanup_function(std::bind(run_cleanup_handler, h));
        }
    }
}}  // End of namespace fibio::fibers
