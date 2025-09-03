## Реализация пула потоков на языке C++

Для начала оговорюсь, что это мой небольшой учебный проект, целью которого является поглубже познакомиться с многопоточностью в с++.

![](thread_pool.png)

Немного поэксперементировав с наивными реализациями я взялся написать более совершенную и удобную реализацию Thread Pool'а, и поставил перед собой цель создать удобный интерфейс взаимодействия пользователя с пулом потоков, разработав при этом достойный формат отдаваемых пулу заданий.

Любую задачу, поступающую на обработку Thread Pool'ом я предлагаю объявлять наследником абстрактного класса `Task`:

```c++
Task {
    // для доступа к TaskStatus из ThreadPool
    friend class ThreadPool;
 public:

    enum class TaskStatus {
        awating,
        completed
    };
    
    // для красивого логирования
    Task(const std::string description_);

    // абстрактный метод, который должен быть реализован пользователем,
    // в теле этой функции должен находиться тракт решения текущей задачи
    void virtual one_thread_method() = 0;   

    // абстрактный метод, который должен быть реализован пользователем,
    // где реализованв вывод в консоль
    void virtual show_result() = 0;
    
    virtual ~Task() = default;

 protected:
    std::string description;

    MT::Task::TaskStatus status;

    size_t task_id;

    // для возможности добавления новых задач в пул прямо из задачи
    ThreadPool* thread_pool;

    // метод, запускаемый потоком
    void one_thread_pre_method();
}
```

Данный подход имеет как преимущества, так и недостатки.

Плюсы:

* Объект задачи уже внутри себя содержит все необходимые ресурсы для ее решения, а потокам пула достаточно лишь вызвать метод `Task::one_thread_pre_method()`, не принимающий никаких лишних аргументов и не возвращающий значений.

* В силу наличия свойства абстрактности у `Task`, запрещено создавать "бесполезные" объекты данного класса.

* Многие реализации в качестве задачи используют шаблонные функции с произвольным числом аргументов вида `template <typename FuncRetType, typename ...Args, typename ...FuncTypes> Task(FuncRetType(*func)(FuncTypes...), Args&&... args)`, что влечет за собой "протягивание" кучи параметров и подвергает проект более тяжелой отладке. В моем случае простейшие, но незаметные ошибки при создании задач будут обнаружены в момент вызова конструктора класса задачи на этапе написания кода, а не на этапе компиляции.

Минусы:

* Под каждый отдельный вид задачи нужно объявлять целый класс. Это может оказаться более громоздким, чем написание новой функции.

* От пользователя требуется владение навыками объектно-ориентированного программирования. Никто не отменял различные проблемы, связанные с наследованием и множественным наследованием.


Прежде переходить к реализации самого пула потоков напишем небольшую обёртку над потоком:

```c++
struct Thread {
        std::thread _thread;
        std::atomic<bool> is_working;

        Thread() : _thread(), is_working(false) {}

        Thread(const std::thread& other) = delete;
        Thread operator=(const Thread& other) = delete;

        Thread(Thread&& other) noexcept : _thread(std::move(other._thread)), 
                                          is_working(other.is_working.load()) {}
    
        Thread& operator=(Thread&& other) noexcept {
            if (this != &other) {
                _thread = std::move(other._thread);
                is_working.store(other.is_working.load());
            }
            return *this;
        }
    };
```

Итак, можно переходить непосредственно к релизации пула потоков - класса `ThreadPool`:

```c++
class ThreadPool {
    public:
    ThreadPool(size_t NUM_THREADS);

    // шаблонная функция добавления задачи в очередь
    template <typename TaskChild>
    size_t add_task(std::shared_ptr<TaskChild> task);


    // получение результата по id (необходимо заранее знать тип возвращаемого объекта)
    template <typename TaskChild>
    std::shared_ptr<TaskChild> get_result(size_t task_id);

    // Для получения результата не зная типа объекта, который лежит под этим id
    void get_result(size_t task_id);

    // Возвращает число работающий потоков
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

    private:
    // мьютексы, блокирующие очереди для потокобезопасного обращения
    std::mutex task_queue_mutex;
    std::mutex completed_tasks_mutex;
    std::mutex incomplete_tasks_with_an_error_mutex;


    // мьютекс для вывода в консоль
    std::mutex cout_mutex;

    // мьютекс, блокирующий функции ожидающие результатов (методы wait*)
    std::mutex wait_mutex;

    // мьютекс, блокирующий логер для последовательного вывода
    std::mutex logger_mutex;

    std::condition_variable tasks_access; 
    std::condition_variable wait_access;   

    // Набор доступных потоков
    std::vector<MT::Thread> threads;

    // Хранит число созданных потоков для их корректного завершения
    size_t actual_threads_count;

    // Очередь задач
    std::queue<std::shared_ptr<Task>> task_queue;
    size_t last_task_id;

    // массив выполненных задач в виде хэш-таблицы
    std::unordered_map<size_t, std::shared_ptr<Task>> completed_tasks;
    size_t completed_task_count;

    // id задач, в которых возникла ошибка при выполнении
    std::unordered_set<size_t> incomplete_tasks_with_an_error;

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

    // Проверка того, выполнены ли все задачи
    bool is_comleted() const;

};
```

Для хранения задач в очереди я использовал "умные" указатели `std::shared_ptr`. Это, в основном, необходимо для преобразования из дочернего класса задачи в базовый класс Task (*upcasting*) при добавлении в очередь и обратно (*downcasting*) при выгрузке результата ("игра на понижение").

Решенные задачи по окончании обработки сохраняются в хеш-таблице ThreadPool::completed_tasks. Такой вариант был выбран из соображений быстрого поиска по идентификатору.

Атрибут `ThreadPool::logger` — это объект вспомогательного класса Logger для логирования. Отношения к рассматриваемой задаче он не имеет, поэтому углубляться в его реализацию я не буду. Все, что он делает, это вычисляет временные интервалы и "красиво" выводит время начала и конца исполнения задачи, а также её описание.

Теперь перейдем к определению методов объявленных классов. С классом `Task` все предельно просто:

```c++

Task(const std::string description_) : description(description_) {
    status = MT::Task::TaskStatus::awating;
    thread_pool = nullptr;
    // Поскольку id задачи будет задачаться самим пулом, то 
    // поле task_id пока что нечем проинициализировать
}

one_thread_pre_method() {
    one_thread_method();
    status = MT::Task::TaskStatus::completed;
}
```

Стоит отметить, что `Task::task_id` свою уникальность приобретает не в момент создания объекта, а в момент добавления в очередь задач (`ThreadPool::add_task`) и никак не контролируется пользователем. Также в момент добавления в очередь с текущей задачей будет связан обрабатывающий ее пул потоков (`Task::thread_pool` по умолчанию `nullptr`). Этот атрибут необходим для обратной связи с пулом, а именно для возможности распараллелить уже исполняемую задачу. 

Теперь поговорим об основном классе `ThreadPool`.

```c++
MT::ThreadPool::ThreadPool(size_t NUM_THREADS) : logger(logger_mutex) {
	paused.store(true);
    stopped.store(false);
    logger_flag.store(true);
	completed_task_count = 0;
    last_task_id = 0;
	threads.reserve(NUM_THREADS);

	for (size_t i : std::ranges::iota_view(static_cast<size_t>(0), NUM_THREADS)) {
		try {
			threads.emplace_back();
		} catch (const std::system_error& e) {
			std::string error_code_str = std::to_string(e.code().value()); 
			std::string error = std::string("When creating thread caught system_error with code ") + "[" + error_code_str + "] meaning " + "[" + e.what() + "]";
			std::time_t error_time = std::time(nullptr);
			
			{
				std::lock_guard<std::mutex> cm(cout_mutex);
				std::cerr << error << '\n';
			}
			std::lock_guard<std::mutex> lm(logger_mutex);
			logger.log_error(error_time, error);
			actual_threads_count = i;
			throw;
		}
	}
	
	for (size_t i : std::ranges::iota_view(static_cast<size_t>(0), NUM_THREADS)) {
		threads[i]._thread = std::move(std::thread(&ThreadPool::run, this, std::ref(threads[i])));
		threads[i].is_working.store(false);
	}
	actual_threads_count = NUM_THREADS;
}
```

При инициализации, пул потоков находится в режиме ожидания (флаг `ThreadPool::paused` по умолчанию `true`).

Флаг `ThreadPool::stopped` должен быть неизменно в состоянии `false` вплоть до вызова деструктора. Он указывает на то, что пул потоков все еще "жив" и способен выполнять поступающие задачи.

Стыдно признаться, но при реализации конструктора я наступил на грабли под названием инвалидация указателей в векторе, поскольку изначатьно просто написал `threads.emplace_back();`, а потом долго и упорно искал ошибку, однако это привнесло и некоторые положительные моменты - я обложил код защитой от исключений во всех местах, где они потенциально могут возникнуть. Поэтому если при попытке открыть очередной поток произойдёт ошибка и будет брошено исклющение, то оно будет успешно обработано, в консоль будет выведено сообщение об ошибке, а также оно будет добавлено в логирующий файл. Также мы запоминаем число реально существующий потоков в переменной `actual_threads_count` - в случае корректной работы оно будет совпадать с `NUM_THREADS`.

Также считаю необходимым отдельно разобрать следующую строчку:
`threads[i]._thread = std::move(std::thread(&ThreadPool::run, this, std::ref(threads[i])));`,


Каждый поток запускается с вызовом закрытого метода `ThreadPool::run`, передавая указатель на самого себя для контроля состояния (управление флагом `Thread::is_working`):
1) Создаём новый поток `std::thread(&ThreadPool::run, this, std::ref(threads[i]))`, насчёт `&ThreadPool::run` всё и так понятно, `this` необходим для вызова этого метода, а `std::ref(threads[i])` - является параметром метода, необходимым для доступа к данным потока (в моём случае это необходимо для получения доступа к флагу `is_working `)
2) Перемещаем вновь созданный поток в член `_thread` структуры `Thread`

Перейдём к реализации метода `run`:

```c++
void MT::ThreadPool::run(MT::Thread& _thread) {
   while (!stopped.load()) {
        std::unique_lock<std::mutex> lock(task_queue_mutex);

        tasks_access.wait(lock, [this]() -> bool { return run_allowed() || stopped.load(); });


        if (run_allowed()) {
            std::shared_ptr<Task> task = std::move(task_queue.front());
            _thread.is_working.store(true);
            task_queue.pop();
			lock.unlock();

			std::time_t start_time = std::time(nullptr);

			try {
            	task->one_thread_pre_method();
			} catch (const std::exception& e) {
				std::time_t error_time = std::time(nullptr);
				std::string error = std::string("Error when solving a problem with an id: ") + std::to_string(task->task_id) + ".\nException: " + e.what();
				std::lock_guard<std::mutex> cm(cout_mutex);
				std::cerr << error << '\n';

				if (logger_flag.load()) {
					std::lock_guard<std::mutex> lm(logger_mutex);
					logger.log_error(error_time, error);
				}
				std::lock_guard<std::mutex> itm(incomplete_tasks_with_an_error_mutex);
				incomplete_tasks_with_an_error.insert(task->task_id);
				continue;
			} catch (...) {
				std::time_t error_time = std::time(nullptr);
				std::lock_guard<std::mutex> cm(cout_mutex);
				std::string error = std::string("Unknown error in task id: ") + std::to_string(task->task_id);
				std::cerr << error << '\n';

				if (logger_flag.load()) {
					std::lock_guard<std::mutex> lm(logger_mutex);
					logger.log_error(error_time, error);
				}
				std::lock_guard<std::mutex> itm(incomplete_tasks_with_an_error_mutex);
				incomplete_tasks_with_an_error.insert(task->task_id);
				continue;
			}

			std::time_t end_time = std::time(nullptr);

            // Логируем выполение задачи
            if (logger_flag.load()) {
				std::lock_guard<std::mutex> lg(logger_mutex);
				logger.add_record_about_task(start_time, end_time, task->description);
			}

            std::lock_guard<std::mutex> lg(completed_tasks_mutex);
			completed_tasks[task->task_id] = std::move(task);
			++completed_task_count;
			_thread.is_working.store(false);
        }
		wait_access.notify_one();
    }
}
```

Пока флаг `stopped == false`, мы пытаемся захватить `task_queue_mutex`, для того, чтобы взять задачу из очереди для последующего выполения, условная переменная `tasks_access`, проверяет, есть ли задачи в очереди, не постален ли он на паузу или же не завешшает ли сервер свою работу, если одно из условий выполено, то мы захватываем `task_queue_mutex` и получаем доступ к очреди задач. Далее мы достаём очередную задачу из очереди и отпускаем мьютекс, затем - исполнение задачи. 
Поскольку `one_thread_pre_method()` потенциально может завершиться неудачно, то необходимо аккуратно обработать такую ситуацию, поскольку мы точно не хотим, чтобы наш пул падал из-за того, что одна из задач завершиласть неудачно. Точно также, как и с ошибками при открытии потоков в контрукторе, мы логируем соощение о неудачном завершении задачи, а также дабавляем её индекс в `incomplete_tasks_with_an_error`, чтобы при попытке получить результат пользователь мог узнать, что была совершена попытка выполнить задачу, но она закончилась неудачно.

Как видно в реализации, решенная задача кладется в `ThreadPool::completed_tasks` в неизменном виде по ключу `Task::task_id`. Мною было решено не вводить излишних усложнений в виде дополнительных классов для сохранения результатов, к тому же совершенно неясно, что будет являться результатом, потому пришлось бы использовать `std::any`. Пользователь самостоятельно определит нужные ему ресурсы в дочернем классе задачи, которые будут изменены или заполнены при помощи переопределенного метода `Task::one_thread_method()`.

```c++
void MT::ThreadPool::start() {
    if (paused.load() == true) {
        paused.store(false);
        // даем всем потокам разрешающий сигнал для доступа к очереди невыполненных задач
        tasks_access.notify_all();
		// логируем
		if (logger_flag) {
			logger.log_start(std::time(nullptr));
		}
    }
    return;
}



void MT::ThreadPool::pause() {
	if (paused.load() == false) {
    	paused.store(true);

		// логируем 
		if (logger_flag) {
			logger.log_paused(std::time(nullptr));
		}
	}
    return;
}
```

Публичные методы `ThreadPool::start()` и `ThreadPool::stop()` нужны для приостановки и возобновления работы пула в том смысле, что устанавливают потоки в режим ожидания и наоборот (важно, что потоки не уничтожаются как объекты).

Далее я продемонстрирую нехитрую реализацию метода для добавления новых задач в очередь на обработку:

```c++
template <typename TaskChild>
size_t add_task(std::shared_ptr<TaskChild> task) {
    std::lock_guard<std::mutex> lock(task_queue_mutex);
    task_queue.push(std::move(task));
    // задаём уникальный идентификатор новой задаче, минимальный id равен 1
    task_queue.back()->task_id = ++last_task_id;

    // Логируем добавление задачи в очередь
    {
        std::lock_guard<std::mutex> cl(cout_mutex);
        std::cout << "Task submitted with ID: " << last_task_id << '\n';
    }
    // связываем задачу с текущим пулом
    task_queue.back()->thread_pool = this;
    tasks_access.notify_one();
    return last_task_id;
}
```

Стоит отметить, что на шаблонный тип `TaskChild` негласно накладывается требование быть дочерним классом базового класса `Task`. Также, здесь с помощью `tasks_access.notify_one()` посылается разрешающий сигнал одному из потоков (неважно какому), который "застрял" в соответствующем месте внутри функции `ThreadPool::run`.

Получить результаты можно по идентификатору задачи только в том случае, если она была решена и направлена в `ThreadPool::completed_tasks`:

```c++
    template <typename TaskChild>
    std::shared_ptr<TaskChild> get_result(size_t task_id) {
        auto it = completed_tasks.find(task_id);
        if (it != completed_tasks.end()) {
            return std::dynamic_pointer_cast<TaskChild>(it->second);
        } else {
            return nullptr;
        }
    }
```

Для корректного преобразования типов внутри `ThreadPool::get_result`, необходимо заранее знать тип задачи.

Также можно воспользоваться нешаблонной перегрузной `ThreadPool::get_result` для вывода результата заачи в консоль:
```c++
void MT::ThreadPool::get_result(size_t task_id) {
	std::lock_guard<std::mutex> lock(completed_tasks_mutex);
	std::lock_guard<std::mutex> cl(cout_mutex);
	auto it = completed_tasks.find(task_id);
	if (it != completed_tasks.end()) {
		std::cout << "Result [" << task_id << "]:\n";
		it->second->show_result();
	} else if (task_id > last_task_id || task_id <= 0) {
		std::cout << "Unknown task ID\n";
	} else {
		std::lock_guard<std::mutex> itm(incomplete_tasks_with_an_error_mutex);
		if (incomplete_tasks_with_an_error.contains(task_id)) {
			std::cout << "An error occurred while completing the task\n";
		} else {
			std::cout << "Result [" << task_id << "]: still processing...\n";
		}
	}
	return;
}
```

Возможны четыре варианта:
- id задачи содержится в `completed_tasks`, тогда будет вызвана виртуальная функция `show_result()`, отображающая результат выполнения задачи
- id меньше нуля или больше общего числа задач, тогда будет выведено сообщение, что такой задачи нет
- id задачи содержится в `incomplete_tasks_with_an_error`, тогда будет выведено сообщение, что при попытке выполнить задачу произошла ошибка
- в противном случае задача выполняется или ждёт своей очереди

```c++
void MT::ThreadPool::wait() {
	std::lock_guard<std::mutex> lock_wait(wait_mutex);

	start();

	std::unique_lock<std::mutex> lock(task_queue_mutex);
	wait_access.wait(lock, [this]()->bool { return is_comleted(); });

	stop();
}
```

Метод `ThreadPool::wait()` позволяет дождаться выполнения всех текущих задач из очереди ThreadPool::task_queue, игнорируя сигналы об остановке.

Остались еще пару вспомогательных методов:

```c++
void MT::ThreadPool::clear_completed() {
	std::lock_guard<std::mutex> lock(completed_tasks_mutex);
	completed_tasks.clear();
}

// Позволяет включать и отключать логирование
void MT::ThreadPool::set_logger_flag(bool flag) {
	logger_flag = flag;
}

// Проверяет, все ли задачи выполнены
bool MT::ThreadPool::is_comleted() const {
	return completed_task_count + incomplete_tasks_with_an_error.size() == last_task_id;
}

// возвращает количество загруженных потоков
size_t MT::ThreadPool::count_working_threads() {
	size_t result = 0;
	for (uint16_t i : std::ranges::iota_view(0u, threads.size())) {
		result += threads[i].is_working.load();
	}
	return result;
}

// возвращает число потоков
size_t MT::ThreadPool::count_of_threads() {
	return threads.size();
}
```

Метод `ThreadPool::clear_completed()` может пригодиться для того, чтобы не хранить ненужные результаты на протяжении всего времени жизни пула. Как я и обещал ранее, флаг `ThreadPool::stopped` становится `true` только в момент уничтожения пула. Собственно говоря, тут и происходит `std::thread::join()` каждого из потоков.

Деструктор:
```c++
MT::ThreadPool::~ThreadPool() {
	wait();
	stopped.store(true);
	tasks_access.notify_all();
	for (size_t i : std::ranges::iota_view(static_cast<size_t>(0), actual_threads_count)) {
        if (threads[i]._thread.joinable()) {
		    try {
                threads[i]._thread.join();
            } catch (const std::system_error& e) {
				std::string error_code_str = std::to_string(e.code().value()); 
				std::string error = std::string("When joining thread caught system_error with code ") + "[" + error_code_str + "] meaning " + "[" + e.what() + "]";
				std::time_t error_time = std::time(nullptr);
				{
					std::lock_guard<std::mutex> cm(cout_mutex);
					std::cerr << error << '\n';
				}
				if (logger_flag.load()) {
					std::lock_guard<std::mutex> lm(logger_mutex);
					logger.log_error(error_time, error);
				}
            }
        }
	}
}
```

Поскольку, как было сказано ранее я старался аккуратно обрабатывать исключения, то и здесь мы сначала проверяем, можно ли присоединить поток, и если да, то присоединяем, если при этом возникают ошибки - корректно их обрабатываем. Ещё стоит сказать, что посколько при открытии потоков могла произойти ошибка, то закрывать мы будем не `NUM_THREADS` потоков, а `actual_threads_count`.

### Пример использования ThreadPool

Я написал небольшую эмуляцию сервера, для демонстрации работы `ThreadPool`:

```c++
TaskType parseType(const std::string& cmd) {
    if (cmd == "compute_primes") return TaskType::ComputePrimes;
    if (cmd == "sort_random") return TaskType::SortRandom;
    if (cmd == "wait_echo") return TaskType::WaitEcho;
    if (cmd == "sort_big_vec") return TaskType::SortBigVec;
	if (cmd == "search_in_file") return TaskType::SearchInALargeFile;
    throw std::runtime_error("Unknown command");
}


int main() {
    MT::ThreadPool thread_pool(5);

    std::cout << "Server started. Enter commands:\n";
  	std::cout << "compute_primes N\n"
          	     "sort_random N\n"
          	     "wait_echo SECONDS MESSAGE\n"
                 "result ID\n"
				 "sort_big_vec\n"
				 "search_in_file\n"
				 "pause - to pause working server\n"
				 "start - to resume working server\n"
				 "count working threads - press '?'\n"
                 "exit\n";
	
	std::string input_data;
	while (std::getline(std::cin, input_data)) {
		std::stringstream ss(input_data);
		std::string command;
		ss >> command;

		if (command == "exit") {
			break;
		} else if (command == "result") {
			uint32_t cur_id = *std::istream_iterator<uint32_t>(ss);
			thread_pool.get_result(cur_id);
		} else if (command == "?") {
			std::cout << thread_pool.count_working_threads() << '\n';
		} else if (command == "pause") {
			thread_pool.pause();
		} else if (command == "start") {
			thread_pool.start();
		}else {
			try {
				TaskType type = parseType(command);
				std::string data;
				std::getline(ss, data);
				if (type == TaskType::ComputePrimes) {
					std::shared_ptr test{std::make_shared<ComputePrimes>(std::stoi(data))};
					thread_pool.add_task(test);
				} else if (type == TaskType::SortRandom) {
					std::shared_ptr test{std::make_shared<SortRandom>(std::stoi(data))};
					thread_pool.add_task(test);
				} else if (type == TaskType::WaitEcho) {
					std::istringstream iss(data);
					uint32_t sec; 
					std::string message;
					iss >> sec;
					std::getline(iss, message);
					message.erase(message.begin());
					std::shared_ptr test{std::make_shared<WaitEcho>(sec, message)};
					thread_pool.add_task(test);
				} else if (type == TaskType::SortBigVec) {
					std::shared_ptr test{std::make_shared<SortBigVec>(std::stoi(data))};
					thread_pool.add_task(std::move(test));
				} if (type == TaskType::SearchInALargeFile) {
					std::string path_to_file, phrase;
					std::cin >> path_to_file >> phrase;
					std::cin.get();
					std::shared_ptr test{std::make_shared<SearchInALargeFile>(path_to_file, phrase)};
					thread_pool.add_task(std::move(test));
				}
		    } catch (std::exception& e) {
				std::cout << "Error: " << e.what() << '\n';
			}
		}
	}
	std::cout << "Server shut down.\n";
    return 0;
}
```

И файлик с задачами, не буду вдаватьсь в подробности реализации, просто опишу их:

#### SortBigVec

Многопоточная задача для сортировки большого числа `int32_t` значений, записанных в файл.

- Создаёт файл с `n` случайными числами.
- Разбивает на чанки (`chunk_size`), каждый сортируется в отдельном потоке.
- Временные отсортированные чанки сохраняются в `temp_files` - текстовые файлы.
- После завершения всех чанков вызывается слияние (`merge_sorted_chunks`) в `../result.txt`.

**Поля:**
- `file_name`: имя исходного файла.
- `dir_name`: временная директория для чанков.
- `temp_files`: пути к временным файлам чанков.
- `chunk_size`: размер одного чанка.
- `n`: количество элементов.
- `completed_chunks`: атомарный счётчик завершённых чанков.
- `temp_files_mutex`, `chunks_cv`: синхронизация между потоками.

**Методы:**
- `read_chunk`: чтение чанка из файла.
- `merge_sorted_chunks`: слияние чанков в итоговый файл.
- `one_thread_method`: логика выполнения задачи.
- `show_result`: проверка корректности сортировки.
- `~SortBigVec`: удаляет временные файлы.

#### SortingChunk

Вспомогательная задача сортировки одного чанка данных.

- Получает на вход вектор `int16_t`, сортирует и сохраняет в файл.
- После сортировки добавляет путь к файлу в `temp_files` родителя (`SortBigVec`).
- Уведомляет `SortBigVec` об окончании работы.

#### ComputePrimes

Задача для вычисления простых чисел от `2` до `n` включительно.

#### SortRandom

Задача генерации и сортировки случайного массива.

#### WaitEcho

Задача, имитирующая задержку и вывод сообщения.

#### SearchInALargeFile

Задача, по реализации аналогичная `SortBigVec`, ищет фразу в тексте, используя алгоритм Кнута-Морриса-Пратта (КМП)

### Проблема deadlock-ов

Во время ручного тестирования проекта я столкнулся с неожиданной проблемой - добавление возможности для класса Task самостоятельно добавлять задачи в пул даёт возможность для паспараллеливания вычислений в рамках одной задачи, но в то же время создаёт неиллюхорную опасность возникновения deadlock-а.
Рассмотрим следующий пример: пусть у нас есть пул, в котором открыты 4 потока, и мы отдадим ему 4 задачи на сортировку вектора, возникает опасность того, что в какой-то момент времени все потоки будут заняты именно этими задачами, которые, в свою очередь, отдадут в пул задачи на сортировку чанков, которые уже некому будет выолнять. 
Возникает ни что иное как deadlock, немного почитав, я понял, что далеко не первый человек, который столкнулся с такой проблемой, и её решение уже давно существует - реализация механизма "воркер-воркер" (worker-worker) или использование отдельной очереди для вложенных задач. Эти решения наиболее эффективно решают возникшую проблему, но мне в голову пришла другая идея, сразу оговорюсь, что она менее эффективная, но я не хочу вносить значимые изменения в архитектуру пула, поэтому остановлюсь именно на ней - реализация вспомогательного класса `ThreadPoolController`. Задачей этого класса будет мониторить ситуацию с пулом и регулярно проверять, не возник ли deadlock, и если он возник - открывать новые потоки в существующем пуле.

Приступим к реализации:

```c++
class ThreadPoolController {
	// Cсылка на пул, за которым мы наблюдаем
	ThreadPool& pool;
	// Поток, в котором будет работать функция, производящая наблюдение за пулом
	std::thread controller_thread;
	std::atomic<bool> stopped{false};

public:

	ThreadPoolController(MT::ThreadPool& pool_ref);

	~ThreadPoolController();

	void monitor();

	void check_for_deadlock();
};
```

Реализация конструктора и деструктора довольна тривиальная, поговорим сразу про функции наблюдения:
```c++
void MT::ThreadPoolController::monitor() {
	while(!stopped.load()) {
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
        check_for_deadlock();
	}
}


void MT::ThreadPoolController::check_for_deadlock() {
	size_t count_of_threads = pool.count_of_threads();
	size_t count_of_waiting_threads = pool.count_waiting_threads();

	if (count_of_threads == count_of_waiting_threads) {
		std::time_t deadlock_time = std::time(nullptr);
		{
			std::lock_guard<std::mutex> cm(pool.cout_mutex);
			std::cerr << "Deadlock detected! Expanding pool...\n";
		}

		if (pool.logger_flag.load()) {
			std::lock_guard<std::mutex> lm(pool.logger_mutex);
			pool.logger.log_deadlock(deadlock_time, pool.count_of_threads());
		}
		pool.expand();
	}
}
```

- `monitor()` - функция, которая раз в 100ms совершает проверку, на возможный deadlock в пуле
- `check_for_deadlock()` - функция проверки наличия dedlock-а, в случае выявлении такового выводит сообщение об этом в консоль, логирует, а также вызывает функция `expand()`, которая увеличивает число потоков

Новые функции в классе `ThreadPool`:
```c++

void MT::ThreadPool::expand() {
	if (count_of_threads() == max_threads) {
		std::string error_str = "The number of open threads has exceeded the maximum allowed limit.";
		std::time_t error_time = std::time(nullptr);

		{
			std::lock_guard<std::mutex> cm(cout_mutex);
			std::cerr << error_str << "\nFurther pool operation is not possible\n";
		}

		if (logger_flag.load()) {
			std::lock_guard<std::mutex> lm(logger_mutex);
			logger.log_error(error_time, error_str);
		}
		throw;
	}
    std::lock_guard<std::mutex> lock(task_queue_mutex);
    try {
		threads.emplace_back();
		threads.back()._thread = std::move(std::thread(&ThreadPool::run, this, std::ref(threads.back())));
		threads.back().is_working.store(false);
		threads.back().is_waiting.store(false);
		actual_threads_count++;
	} catch (const std::system_error& e) {
		std::string error_code_str = std::to_string(e.code().value()); 
		std::string error = std::string("When creating thread caught system_error with code ") + "[" + error_code_str + "] meaning " + "[" + e.what() + "]";
		std::time_t error_time = std::time(nullptr);
		
		{
			std::lock_guard<std::mutex> cm(cout_mutex);
			std::cerr << error << '\n';
		}
		std::lock_guard<std::mutex> lm(logger_mutex);
		logger.log_error(error_time, error);
		--actual_threads_count;
		throw;
	}
}

// устанавливает флаг is_waiting для вызывающего потока
void MT::ThreadPool::set_current_thread_waiting(bool waiting_status) {
	std::thread::id current_id = std::this_thread::get_id();
	for (auto& thread : threads) {
		if (thread._thread.get_id() == current_id) {
			thread.is_waiting.store(waiting_status);
			return;
		}
	}
}

// возврвщает число ожидающих потоков
size_t MT::ThreadPool::count_waiting_threads() {
	size_t result = 0;
	for (uint16_t i : std::ranges::iota_view(0u, threads.size())) {
		result += threads[i].is_waiting.load();
	}
	return result;
}
```

`expand()` - увеличивает число потоков на один, в случаевозникновении ошибок при создании нового потока - логирует и вывыводит в консоль сообщение об ошибке. Также я заложил максимально допустимое число потоков - max_threads, оно необходимо как чисто с архитектурной точки зрения - я не могу делать `emplase()` в `threads` предварительно не выделив память под все потоки, в противном случае ссылки, которые мы отдали в `ThreadPool::run()` инвалидируются, так и число с логической, навряд ли мы хотим открытия 10000 потоков...

Также небольшим изменениям подверглись поля классов `ThreadPool` и `Thread`. В первый мы добавили поля `const size_t max_threads = 100`, а также `MT::ThreadPoolController controller;` - объект контроллера. Во второй - поле `std::atomic<bool> is_waiting;`

Наконец немного изменился конструктор класса `ThreadPool` - добавлена инициализация `controller`, а также инициализацию флага `is_waiting` класса `Thread`.
