/*
 * Copyright (C) 2022, 2023  T+A elektroakustik GmbH & Co. KG
 *
 * This file is part of T+A Streamplayer.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA  02110-1301, USA.
 */

#ifndef RNFCALL_HH
#define RNFCALL_HH

#include "rnfcall_state.hh"
#include "logged_lock.hh"

#include <functional>
#include <future>
#include <string>

namespace DBusRNF
{

struct BadStateError: public std::runtime_error { BadStateError(): runtime_error("DBusRNF::BadStateError") {} };
struct AbortedError:  public std::runtime_error { AbortedError():  runtime_error("DBusRNF::AbortedError") {} };
struct NoResultError: public std::runtime_error { NoResultError(): runtime_error("DBusRNF::NoResultError") {} };

class CallBase;

/*!
 * Base class for extra data and notifications used during RNF calls.
 *
 * #ContextData objects are used for associating any extra data (or even
 * functionality) with an RNF call, and passing these back to the caller when
 * the call finishes. Any data may be added to derived classes for use in
 * client code which processes the RNF call results.
 *
 * For convenience, the caller may want to use a #DBusRNF::Chain object if it
 * doesn't require more data than can conveniently be passed via a lambda
 * function. The #DBusRNF::Chain class merely specializes the notification
 * function and calls it so that no extra downcast is required.
 */
class ContextData
{
  public:
    using NotificationFunction = std::function<void(CallBase &call, CallState state)>;

  private:
    /*!
     * Function object called from classes derived from #DBusRNF::CallBase.
     *
     * Note that composition, not inheritance is key here. This allows us to
     * use the same class from various contexts with different notification
     * needs.
     */
    const NotificationFunction notify_fn_;

  protected:
    /*!
     * Context data constructor to be used by derived classes.
     *
     * \param notify_fn
     *     Function is called after a result for an RNF method call has been
     *     fetched. The function can be sure that a definite result for the
     *     call is available, either as usable data or as an error. This is a
     *     good place for calling #DBusRNF::Call::get_result_unlocked(). May be
     *     \c nullptr.
     */
    explicit ContextData(NotificationFunction &&notify_fn):
        notify_fn_(std::move(notify_fn))
    {}

  public:
    ContextData(ContextData &&) = default;

    virtual ~ContextData() = default;

    /*!
     * Call notification function if set.
     */
    virtual void notify(CallBase &call, CallState state)
    {
        if(notify_fn_ != nullptr)
            notify_fn_(call, state);
    }
};

/*!
 * Generic context data for simple notification.
 *
 * This class extends from #DBusRNF::ContextData, and adds nothing but a
 * specialized notification callback. The \c call is converted to the type
 * passed in the \p CallType template parameter, adding type-safety and freeing
 * the callback from downcasting the #DBusRNF::CallBase reference to the
 * correct derived type.
 *
 * Frequently, client code which requires notification can get away by using
 * this class template and passing any extra data by a lambda function. It is
 * often not necessary to craft specializations from #DBusRNF::ContextData.
 */
template <typename CallType>
class Chain: public ContextData
{
  public:
    Chain(Chain &&) = default;
    Chain &operator=(Chain &&) = default;

    explicit Chain(std::function<void(CallType &call, CallState state)> &&notify_fn):
        ContextData([fn = std::move(notify_fn)] (CallBase &call, CallState state)
                    { fn(static_cast<CallType &>(call), state); })
    {}

    virtual ~Chain() = default;
};

/*!
 * Function type used for reporting RNF call state changes.
 *
 * This function is called with the #DBusRNF::CallBase object locked.
 * Attempting to lock it inside this function will lead to a deadlock
 * situation.
 */
using StatusWatcher =
    std::function<void(const CallBase &call, CallState state, bool is_detached)>;


/*!
 * Base class for RNF-style D-Bus method invocation.
 *
 * First of all, an object of this class stores a state and a data cookie. The
 * data cookie is set once the first request has returned a data cookie, i.e.,
 * in which case the caller must wait for notification and fetch the result
 * with a separate call. The cookie remains invalid if the first request has
 * returned data already. The object state keeps track of request state.
 *
 * This class also stores a function object which knows how to abort the
 * request associated with the data cookie.
 *
 * Further, there are three different notification facilities which allow
 * callers to keep track of the call state: (1) a status watcher function
 * object; (2) a context data object; and (3) a condition variable.
 *
 * The status watcher function is passed to the constructor. It is called
 * whenever the object state changes, including the initial state "change" in
 * the constructor. This is convenient for waiting for completion or failure in
 * the background.
 *
 * The context data object is of a type derived from #DBusRNF::ContextData, and
 * like the status watcher function it is also passed to the constructor. The
 * primary purpose of this object is to store extra data dependent on the
 * context in which the #DBusRNF::CallBase object has been created, which is
 * required at some later point when the requested results are available. The
 * context data object also has a virtual #DBusRNF::ContextData::notify()
 * function member which may be called from classes derived from
 * #DBusRNF::CallBase the way it needs to (most notably, #DBusRNF::CookieCall
 * does this, which calls the notification function on request completion).
 *
 * The condition variable is available to derived classes, not directly to
 * callers. It is notified when a request has finished, either successfully or
 * with a definite error. The condition variable can be used to implement
 * blocking calls (see #DBusRNF::Call::fetch_blocking()).
 *
 * Derived classes must call DBusRNF::CallBase::abort_request() in their
 * destructor. This function cannot be called from the base destructor because
 * it calls virtual functions, so the call would run into UB, and likely, the
 * program would crash. It is safest to push down the function call into the
 * destructor of the most derived class. Making these destructors \c final is a
 * good idea to keep them from being overridden by future extensions.
 */
class CallBase
{
  private:
    CallState state_;
    bool was_aborted_after_done_;
    bool detached_;

  public:
    mutable LoggedLock::Mutex lock_;
  protected:
    LoggedLock::ConditionVariable notified_;

  private:
    uint32_t cookie_;
    uint32_t cleared_cookie_;

  protected:
    const std::function<bool(uint32_t)> abort_cookie_fn_;
    std::unique_ptr<ContextData> context_data_;
    StatusWatcher status_watcher_fn_;

    explicit CallBase(std::function<bool(uint32_t)> &&abort_cookie_fn,
                      std::unique_ptr<ContextData> context_data,
                      StatusWatcher &&status_watcher_fn):
        state_(CallState::INITIALIZED),
        was_aborted_after_done_(false),
        detached_(false),
        cookie_(0),
        cleared_cookie_(0),
        abort_cookie_fn_(std::move(abort_cookie_fn)),
        context_data_(std::move(context_data)),
        status_watcher_fn_(std::move(status_watcher_fn))
    {
        LoggedLock::configure(lock_, "DBusRNF::CallBase-lock", MESSAGE_LEVEL_DEBUG);
        LoggedLock::configure(notified_, "DBusRNF::CallBase-cv", MESSAGE_LEVEL_DEBUG);

        if(status_watcher_fn_ != nullptr)
            status_watcher_fn_(*this, state_, detached_);
    }

  public:
    CallBase(const CallBase &) = delete;

    virtual ~CallBase()
    {
        switch(state_)
        {
          case CallState::WAIT_FOR_NOTIFICATION:
          case CallState::READY_TO_FETCH:
            MSG_BUG("Destroying RNF call with active cookie %u", cookie_);
            break;

          case CallState::INITIALIZED:
          case CallState::RESULT_FETCHED:
          case CallState::ABORTING:
          case CallState::ABORTED_BY_PEER:
          case CallState::FAILED:
          case CallState::ABOUT_TO_DESTROY:
            break;
        }

        set_state(CallState::ABOUT_TO_DESTROY);
    }

    template <typename T>
    T &get_data()
    {
        if(context_data_ != nullptr)
            return *dynamic_cast<T *>(context_data_.get());

        throw std::runtime_error("No context data");
    }

    bool abort_request();

    void abort_request_on_destroy();

    void result_available_notification(uint32_t cookie)
    {
        notification(cookie, CallState::READY_TO_FETCH, "Ready");
    }

    void aborted_notification(uint32_t cookie)
    {
        notification(cookie, CallState::ABORTED_BY_PEER, "Abort");
    }

    virtual std::string get_description() const;

  protected:
    CallState get_state() const { return state_; }

    void set_state(CallState new_state)
    {
        if(state_ == new_state)
            return;

        state_ = new_state;

        if(status_watcher_fn_ != nullptr)
            status_watcher_fn_(*this, state_, detached_);
    }

    bool was_aborted_after_done() const { return was_aborted_after_done_; }

    void detached()
    {
        msg_log_assert(detached_ == false);
        detached_ = true;
    }

    void set_cookie(uint32_t cookie)
    {
        msg_log_assert(cookie != 0);
        cookie_ = cookie;
    }

    uint32_t clear_cookie()
    {
        cleared_cookie_ = cookie_;
        cookie_ = 0;
        return cleared_cookie_;
    }

    uint32_t get_cookie() const { return cookie_; }

    bool abort_request_internal(bool suppress_errors);

    /*!
     * Get name for debugging.
     *
     * Contract: This function never returns \c nullptr.
     */
    virtual const char *name() const { return "*no name*"; }

  private:
    void notification(uint32_t cookie, CallState new_state, const char *what);
};

/*!
 * Class template for RNF-style D-Bus methods with their specific return type.
 *
 * This class implements the fundamentals of the Request and Fetch parts in
 * RNF, and supports implementation of the Notification part.
 *
 * Requests are started by #DBusRNF::Call::request(), which uses a function
 * object to perform the actual D-Bus method call for the request (note that
 * there is actually no D-Bus-specific code in here, so the method call made by
 * the function object could be any call, really). A fast-path answer, if any,
 * is stored in a \c std::promise object owned by this class template. If there
 * is no fast-path answer, then there will be a cookie instead representing the
 * answer; in this case, the cookie is passed to a cookie management object.
 *
 * Notifications are expected to be triggered by client code which actually
 * receives the deferred answer to the request. Client code must call one of
 * #DBusRNF::CallBase::result_available_notification() or
 * #DBusRNF::CallBase::aborted_notification() to indicate availability of a
 * result for the request. In general, client code will have to map the cookie
 * received by the receiver of the D-Bus request and reported back during the
 * request notification to the #DBusRNF::Call object which belongs to the
 * cookie.
 *
 * Fetching the result is done by a call of #DBusRNF::Call::fetch() or
 * #DBusRNF::Call::fetch_blocking(), where the latter will block until
 * completion of the request in case the notification hasn't been sent yet.
 * Both functions use a function object which performs the actual D-Bus method
 * call for fetching the result by cookie, and places the result into an object
 * of type \p RT (i.e., #DBusRNF::Call::ResultType). After this, the result is
 * stored in the #DBusRNF::Call object and can be retrieved by client code
 * using either #DBusRNF::Call::get_result_locked() or
 * #DBusRNF::Call::get_result_unlocked().
 *
 * Fetching is not performed if the result is already available, i.e., it is
 * safe to try to fetch results even if the result has previously been made
 * available as a fast-path answer or in case the call was aborted on behalf of
 * any party.
 *
 * Client code may explicitly abort an RNF method call by calling
 * #DBusRNF::CallBase::abort_request() at any time.
 *
 * Errors are communicated by exceptions in all stages. Functions for
 * requesting and fetching throw directly if and only if the #DBusRNF::Call
 * object involved is used incorrectly; these exceptions hint at programming
 * errors which must be fixed. Exceptions thrown by the helper functions which
 * implement the details of method calls are caught and stored in the internal
 * \c std::promise object; these exceptions are runtime errors (usually, these
 * will be #List::DBusListException errors) and must be caught and handled by
 * client code when the #DBusRNF::Call::get_result_locked() or
 * #DBusRNF::Call::get_result_unlocked() function is called. These functions
 * are the only ones which will (re-)throw detailed D-Bus exceptions.
 *
 * The #DBusRNF::CookieCall class template is a useful specialization of
 * #DBusRNF::Call which simplifies the use of #DBusRNF::Call::request(),
 * #DBusRNF::Call::fetch(), and #DBusRNF::Call::fetch_blocking(), and also
 * integrates cookie management using a #DBusRNF::CookieManagerIface object.
 * For most use cases the #DBusRNF::CookieCall class template should be used.
 *
 * \tparam RT
 *     The result type. This might be any type, be it a custom class for more
 *     complex return types, or e.g. a \c std::tuple, \c std::pair, or even a
 *     fundamental type for simpler results.
 */
template <typename RT>
class Call: public CallBase, public std::enable_shared_from_this<Call<RT>>
{
  public:
    using ResultType = RT;

  protected:
    std::promise<ResultType> promise_;
    std::future<ResultType> future_;

  public:
    Call(Call &&) = default;
    Call &operator=(Call &&) = default;

    explicit Call(std::function<bool(uint32_t)> &&abort_cookie_fn,
                  std::unique_ptr<ContextData> context_data,
                  StatusWatcher &&status_watcher):
        CallBase(std::move(abort_cookie_fn), std::move(context_data),
                 std::move(status_watcher)),
        future_(promise_.get_future())
    {}

    /*!
     * Request some data from list broker via D-Bus.
     *
     * \param block_async_result_notifications
     *     Function for blocking processing of asynchronous cookie result
     *     notifications, returning a corresponding lock. This is required to
     *     avoid race conditions.
     *
     * \param do_request
     *     The code for sending the request and processing a possible fast-path
     *     answer is to be implemented in this function. The #DBusRNF::Call
     *     class template itself does not implement any specific request (it
     *     only manages the specific request). Exceptions thrown by
     *     \p do_request() are caught and stored in the promise; they are not
     *     re-thrown.
     *
     * \param manage_cookie
     *     This function is called if and only if the D-Bus request call has
     *     been made, and if it has returned a valid cookie for later data
     *     retrieval. The cookie is passed to this function. It is an error to
     *     pass \c nullptr.
     *
     * \param fast_path
     *     This function is called if and only if the D-Bus request call has
     *     been made, and if it has returned the requested data (and thus, also
     *     an invalid cookie). It is an error to pass \c nullptr.
     *
     * Requirements for \p do_request():
     * * The function is supposed to make a synchronous call of a D-Bus method
     *   which, on success, returns either some data (data cookie will be 0 in
     *   this case) or a valid, non-zero data cookie (returned data are dummies
     *   and must be discarded in this case).
     * * The function must return the data cookie returned by the D-Bus method.
     * * The function must set the promise passed to it if and only if the
     *   D-Bus method has returned 0 for the data cookie, in which case the
     *   D-Bus method has returned the requested data. The promise is supposed
     *   to be set from the values returned by the D-Bus method.
     * * In case the D-Bus method call fails, the function must throw an
     *   exception. Function #DBusRNF::Call::request() will put it into the
     *   promise.
     *
     * \returns
     *     The new state of the #DBusRNF::Call object. In case
     *     #DBusRNF::CallState::WAIT_FOR_NOTIFICATION is returned, the
     *     requested data are not available yet and must be retrieved via
     *     #DBusRNF::Call::fetch() after the D-Bus callee has notified
     *     completion. In case #DBusRNF::CallState::RESULT_FETCHED is
     *     returned, the requested data are available via fast-path answer and
     *     can be read out using #DBusRNF::Call::get_result_locked() or
     *     #DBusRNF::Call::get_result_unlocked().
     *
     * \throws
     *     #DBusRNF::BadStateError The object is in wrong state.
     */
    CallState request(
            const std::function<LoggedLock::UniqueLock<LoggedLock::RecMutex>()> &block_async_result_notifications,
            const std::function<uint32_t(std::promise<ResultType> &)> &do_request,
            const std::function<void(uint32_t)> &manage_cookie,
            const std::function<void()> &fast_path)
    {
        LOGGED_LOCK_CONTEXT_HINT;
        std::lock_guard<LoggedLock::Mutex> lock(lock_);

        if(get_state() != CallState::INITIALIZED)
        {
            MSG_BUG("RNF request in state %u", int(get_state()));
            throw BadStateError();
        }

        const auto t(this->shared_from_this());

        /*
         * The call of \p do_request() below may cause an asynchronous cookie
         * available notification being delivered to the D-Bus thread. This
         * happens if the RNF call is not answered via fath path, but the
         * availability notification is still being sent before
         * \p manage_cookie() can be called. This is a data race which is
         * avoided by bracing this section of code by calls of
         * \p block_async_result_notifications().
         *
         * On the slow CM1, this happens frequently when the system is loaded
         * as a result of intense user interaction.
         */
        auto result_notification_lock(block_async_result_notifications());

        try
        {
            const uint32_t cookie = do_request(promise_);

            if(cookie != 0)
            {
                set_cookie(cookie);
                detached();
                manage_cookie(cookie);
            }
            else
                msg_log_assert(get_cookie() == 0);

            set_state(cookie == 0 ? CallState::RESULT_FETCHED : CallState::WAIT_FOR_NOTIFICATION);
            result_notification_lock.unlock();

            if(cookie == 0)
                fast_path();
        }
        catch(...)
        {
            msg_error(0, LOG_NOTICE, "RNF request %s failed", name());

            try
            {
                promise_.set_exception(std::current_exception());
            }
            catch(...)
            {
                MSG_BUG("Double exception on RNF request failure");
            }

            set_state(CallState::FAILED);
        }

        return get_state();
    }

    /*!
     * Fetch results by cookie after ready notification was sent.
     *
     * \param do_fetch
     *     The code for sending the request by cookie and processing the answer
     *     is to be implemented in this function. Exceptions thrown by
     *     \p do_fetch() are caught and stored in the promise; they are not
     *     re-thrown.
     *
     * Requirements for \p do_fetch():
     * * The function is supposed to make a synchronous call of a D-Bus method
     *   which, on success, returns data associated with the cookie previously
     *   returned by \p do_request() (see #DBusRNF::Call::request()).
     * * The function must set the promise passed to it if and only if the
     *   D-Bus method has returned successfully. The promise is supposed to be
     *   set from the values returned by the D-Bus method.
     * * In case the D-Bus method call fails, the function must throw an
     *   exception. Function #DBusRNF::Call::fetch() will put it into the
     *   promise.
     *
     * \throws
     *     #DBusRNF::BadStateError The object is in wrong state.
     */
    bool fetch(const std::function<void(uint32_t, std::promise<ResultType> &)> &do_fetch)
    {
        LOGGED_LOCK_CONTEXT_HINT;
        std::lock_guard<LoggedLock::Mutex> lock(lock_);
        return fetch_unlocked(do_fetch);
    }

    /*!
     * Wait for any ready notification, then fetch results by cookie.
     *
     * In case the preceding request returned a data cookie, this function will
     * block until either #DBusRNF::CallBase::result_available_notification()
     * or #DBusRNF::CallBase::aborted_notification() is called from another
     * thread.
     *
     * Note that this function may block for an indefinite amount time.
     *
     * \see
     *     DBusRNF::Call::fetch()
     */
    bool fetch_blocking(const std::function<void(uint32_t, std::promise<ResultType> &)> &do_fetch)
    {
        LOGGED_LOCK_CONTEXT_HINT;
        LoggedLock::UniqueLock<LoggedLock::Mutex> lock(lock_);

        /* XXX: Possibly error out hard after a very pessimistic upper boundary
         *      of 10 minutes to keep the system in a workable state. */
        using namespace std::chrono_literals;
        while(!notified_.wait_for(
                lock, 20s,
                [this] { return get_state() != CallState::WAIT_FOR_NOTIFICATION; }))
        {
            msg_error(ETIME, LOG_NOTICE,
                      "RNF call %p (%s) still waiting for notification in state %d",
                      static_cast<void *>(this), name(), int(get_state()));
        }

        return fetch_unlocked(do_fetch);
    }

  protected:
    /*!
     * Fetch the result of the call.
     *
     * The caller of this function must hold the lock.
     */
    bool fetch_unlocked(const std::function<void(uint32_t, std::promise<ResultType> &)> &do_fetch)
    {
        switch(get_state())
        {
          case CallState::READY_TO_FETCH:
            break;

          case CallState::RESULT_FETCHED:
            return true;

          case CallState::ABORTING:
          case CallState::ABORTED_BY_PEER:
          case CallState::FAILED:
            return false;

          case CallState::INITIALIZED:
          case CallState::WAIT_FOR_NOTIFICATION:
          case CallState::ABOUT_TO_DESTROY:
            MSG_BUG("RNF request in state %u", int(get_state()));
            throw BadStateError();
        }

        try
        {
            do_fetch(get_cookie(), promise_);
        }
        catch(...)
        {
            msg_error(0, LOG_NOTICE, "RNF request %s failed", name());

            try
            {
                promise_.set_exception(std::current_exception());
            }
            catch(...)
            {
                MSG_BUG("Double exception on RNF fetch failure");
            }

            clear_cookie();
            set_state(CallState::FAILED);

            return false;
        }

        clear_cookie();
        set_state(CallState::RESULT_FETCHED);

        return true;
    }

  public:
    /*!
     * Retrieve the result of the D-Bus call.
     *
     * This function may only be called after a request has been made and the
     * result is known to be available, i.e., in case #DBusRNF::Call::request()
     * has returned #DBusRNF::CallState::RESULT_FETCHED or
     * #DBusRNF::Call::fetch() has returned successfully.
     *
     * \returns
     *     The result filled in either from data returned from the initial
     *     D-Bus call or from data returned from the second request by cookie.
     *
     * \throws
     *     #DBusRNF::AbortedError The callee has notified abortion of the
     *         request.
     *
     * \throws
     *     #DBusRNF::BadStateError Wrong object state.
     *
     * \throws
     *     #DBusRNF::NoResultError There is no result stored. This should never
     *         happen unless there is some bug.
     *
     * \throws
     *     Exception Any exception previously thrown by the \p do_request()
     *         function passed to #DBusRNF::Call::request() or the
     *         \p do_fetch() function passed to #DBusRNF::Call::fetch() or
     *         #DBusRNF::Call::fetch_blocking().
     */
    ResultType get_result_locked()
    {
        std::lock_guard<LoggedLock::Mutex> lock(lock_);
        return get_result_unlocked();
    }

    /*!
     * Retrieve the result of the D-Bus call, don't acquire lock.
     *
     * Variant of #DBusRNF::Call::get_result_locked() which can be used from
     * status watchers, i.e., in context which already hold the call object
     * lock. Client code should use #DBusRNF::Call::get_result_locked() by
     * default unless use of the unlocked variant is mandated by context.
     */
    ResultType get_result_unlocked()
    {
        switch(get_state())
        {
          case CallState::RESULT_FETCHED:
          case CallState::FAILED:
            break;

          case CallState::INITIALIZED:
          case CallState::WAIT_FOR_NOTIFICATION:
          case CallState::READY_TO_FETCH:
          case CallState::ABOUT_TO_DESTROY:
            throw BadStateError();

          case CallState::ABORTING:
          case CallState::ABORTED_BY_PEER:
            throw AbortedError();
        }

        if(was_aborted_after_done())
            throw AbortedError();

        if(!future_.valid())
            throw NoResultError();

        try
        {
            return future_.get();
        }
        catch(...)
        {
            set_state(CallState::FAILED);
            throw;
        }
    }
};

}

#endif /* !RNFCALL_HH */
