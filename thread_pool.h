#pragma once 
#include <iostream>
#include <cstdint>
#include <ranges>
#include <vector>
#include <queue>
#include <unordered_map>
#include <condition_variable>
#include <thread>
#include <mutex>
#include "Logger.h"


namespace MT {

    class ThreadPool;

    // Нужен класс - обёртка для задачи
    class Task {
        friend class ThreadPool;
     public:
        enum class TaskStatus {
            awating,
            completed
        };

        Task(const std::string& description_);

        // абстрактный метод, который должен быть реализован пользователем,
        // в теле этой функции должен находиться тракт решения текущей задачи
        void virtual one_thread_method() = 0;   

        // абстрактный метод, который должен быть реализован пользователем,
        // где реализованв вывод в консоль
        void virtual show_result() = 0;

        virtual ~Task() = default;

     protected:

        // Для красивого логирования
        std::string description;

        size_t task_id;

        MT::Task::TaskStatus status;

        //для возможности добавления новых задач в пул прямо из задачи
        MT::ThreadPool* thread_pool; 

        // метод, запускаемый потоком
        void one_thread_pre_method();
    };


    // Обёртка для потока
    struct Thread {
        std::thread _thread;
        std::atomic<bool> is_working;

        Thread() : _thread(), is_working(false) {}

        Thread(const std::thread& other) = delete;
        Thread operator=(const Thread& other) = delete;

        Thread(Thread&& other) noexcept : _thread(std::move(other._thread)), is_working(other.is_working.load()) {	}
        
        Thread& operator=(Thread&& other) noexcept {
            if (this != &other) {
                _thread = std::move(other._thread);
                is_working.store(other.is_working.load());
            }
            return *this;
        }
    };




    class ThreadPool {
     public:
        ThreadPool(size_t NUM_THREADS);

        // шаблонная функция добавления задачи в очередь
		template <typename TaskChild>
		size_t add_task(std::shared_ptr<TaskChild> task) {
			std::lock_guard<std::mutex> lock(task_queue_mutex);
			task_queue.push(std::move(task));
			// задаём уникальный идентификатор новой задаче, минимальный id равен 1
			task_queue.back()->task_id = ++last_task_id;
			// связываем задачу с текущим пулом
            {
                std::lock_guard<std::mutex> cl(cout_mutex);
			    std::cout << "Task submitted with ID: " << last_task_id << '\n';
            }
			task_queue.back()->thread_pool = this;
			tasks_access.notify_one();
			return last_task_id;
		}


        // получение результата по id (необходимо заранее знать тип возвращаемого объекта)
		template <typename TaskChild>
		std::shared_ptr<TaskChild> get_result(size_t task_id) {
	        std::lock_guard<std::mutex> lock(completed_tasks_mutex);
			auto it = completed_tasks.find(task_id);
			if (it != completed_tasks.end()) {
				return std::dynamic_pointer_cast<TaskChild>(it->second);
            } else {
				return nullptr;
            }
		}


        // Для получения результата не зная типа объекта, который лежит под этим id
        void get_result(size_t task_id);

        size_t count_working_threads();

        // приостановка обработки
		void pause();

		// возобновление обработки
		void start();

        void wait();

        // очистить выполненные задач
		void clear_completed();

        void set_logger_flag(bool flag);

        size_t count_of_threads();

        ~ThreadPool();

        // мьютекс для вывода в консоль
        std::mutex cout_mutex;


     private:
        // мьютексы, блокирующие очереди для потокобезопасного обращения
        std::mutex task_queue_mutex;
        std::mutex completed_tasks_mutex;

        // мьютекс, блокирующий функции ожидающие результатов (методы wait*)
        std::mutex wait_mutex;

        // мьютекс, блокирующий логер для последовательного вывода
		std::mutex logger_mutex;

        std::condition_variable tasks_access; 
        std::condition_variable wait_access;   

        // Набор доступных потоков
        std::vector<MT::Thread> threads;

        // Очередь задач
        std::queue<std::shared_ptr<Task>> task_queue;
        size_t last_task_id;

        // массив выполненных задач в виде хэш-таблицы
		std::unordered_map<size_t, std::shared_ptr<Task>> completed_tasks;
		size_t completed_task_count;

        // флаг остановки работы пула
        std::atomic<bool> stopped;
        // флаг логирования - способ отключить логирование
        std::atomic<bool> logger_flag;
        // флаг приостановки работы
        std::atomic<bool> paused;

        Logger logger;

        // основная функция, инициализирующая каждый поток
		void run(MT::Thread& thread);

        // разрешение запуска очередного потока
		bool run_allowed() const;

        bool is_comleted() const;

    };
}