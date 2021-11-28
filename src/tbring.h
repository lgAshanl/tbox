#include <mutex>
#include <condition_variable>

#pragma once

namespace TB
{
    template<class T>
    class MTRingPtr
    {
        struct Node
        {
            //Node* m_next;
            Node* m_prev;

            T* m_value;
        };

    public:

        MTRingPtr()
          : m_lock(),
            m_head(nullptr)
        { }

        ~MTRingPtr()
        {
            T* value = pop();
            while (nullptr != value)
            {
                delete value;
                value = pop();
            }
        }

        void push(T* value)
        {
            Node* node = new Node();
            node->m_value = value;

            std::lock_guard<std::mutex> g(m_lock);
            
            //node->m_next = m_tail;
            node->m_prev = nullptr;

            if (nullptr == m_head)
            {
                m_head = node;
            }
            else
            {
                m_tail->m_prev = node;
            }

            m_tail = node;
        }

        T* pop()
        {
            Node* node;
            {
                std::lock_guard<std::mutex> g(m_lock);
                if (nullptr == m_head)
                    return nullptr;

                node = m_head;

                m_head = node->m_prev;

                if (nullptr == m_head)
                {
                    m_tail = nullptr;
                }
            }

            T* res = node->m_value;
            delete node;

            return res;
        }

        inline bool isEmpty()
        {
            std::lock_guard<std::mutex> g(m_lock);
            return (nullptr == m_head);
        }

    private:

        // heresy
        std::mutex m_lock;

        Node* m_head;
        Node* m_tail;
    };

    class TBEvent
    {
    public:

        TBEvent()
          : m_mutex(),
            m_condvar(),
            m_sync(0)
        {
        }

        TBEvent(const TBEvent& other) = delete;
        TBEvent(TBEvent&& other) noexcept = delete;
        TBEvent& operator=(const TBEvent& other) = delete;
        TBEvent& operator=(TBEvent&& other) noexcept = delete;

        inline void reinit();

        inline void set();

        inline void wait();

    private:
        std::mutex m_mutex;

        std::condition_variable m_condvar;

        int32_t m_sync;
    };

    void TBEvent::reinit()
    {
        m_sync = 0;
    }

    void TBEvent::set()
    {
        std::lock_guard<std::mutex> g(m_mutex);
        m_sync = 1;
        m_condvar.notify_all();
    }

    void TBEvent::wait()
    {
        std::unique_lock<std::mutex> g(m_mutex);
        m_condvar.wait(g, [&] {return m_sync == 1;});
    }
}