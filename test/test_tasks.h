#include <iostream>
#include <random>
#include <vector>
#include <algorithm>
#include <ranges>
#include <iterator>
#include <map>
#include <filesystem>
#include "../thread_pool.h"



// Тип задачи
enum class TaskType {
	ComputePrimes, SortRandom, WaitEcho, SortBigVec, SearchInALargeFile
};


// Генерируем и сортируем массив из n элементов
struct SortRandom : public MT::Task {
    std::vector<int16_t> arr;
    size_t n;

    SortRandom(size_t n_);

    void one_thread_method() override;
    void show_result() override;
};



// Ищем все простые числа, меньшие, чем n
struct ComputePrimes : public MT::Task {
    std::vector<uint64_t> arr;
    size_t n;

    ComputePrimes(size_t n_);

    void one_thread_method() override;
    void show_result() override;
};



// блокируем поток на заданное кол-во секунд
struct WaitEcho : public MT::Task {
    size_t seconds;

    WaitEcho(size_t seconds_, const std::string message_);

    void one_thread_method() override;
    void show_result() override;
};



class SortingChunk;

// Сотрировка элементов файла, при этом многопоточная
class SortBigVec : public MT::Task {
    std::mutex temp_files_mutex;
    std::condition_variable chunks_cv;
    std::atomic<size_t> completed_chunks{0};

    std::string file_name;
    std::filesystem::path dir_name;

    std::vector<std::string> temp_files;
    const size_t chunk_size = 1'000'000;
    size_t file_id;
    size_t n;

    friend class SortingChunk;

 public:

    SortBigVec(size_t n_ = 1'000'000u);

    std::vector<int16_t> read_chunk(size_t chunk_size, std::ifstream& file);
    void merge_sorted_chunks();
    void one_thread_method() override;
    void show_result() override;

    ~SortBigVec();
};


class SortingChunk : public MT::Task {
    std::vector<int16_t> arr; 
    SortBigVec& parrent;

 public:
    SortingChunk(std::vector<int16_t>& arr_, SortBigVec& parrent_);

    void one_thread_method() override;
    void show_result() override;
};






class SearchInAChunk;

class SearchInALargeFile : public MT::Task {
    std::map<size_t, std::pair<std::string, size_t>> information_found;
    std::string path_to_file;
    std::string word;

    std::mutex information_found_mutex;
    std::condition_variable information_cv;
    std::atomic<size_t> completed_chunks{0};

    size_t chunk_size = 100;

    friend class SearchInAChunk;

 public:

    SearchInALargeFile(const std::string& path_to_file_, const std::string& phrase_);

    void one_thread_method() override;
    void show_result() override;

};


class SearchInAChunk : public MT::Task {
    SearchInALargeFile& parrent;
    std::vector<std::pair<size_t, std::string>> chunc;

 public:

    SearchInAChunk(SearchInALargeFile& parrent_, std::vector<std::pair<size_t, std::string>>&& chunc);

    void one_thread_method() override;
    void show_result() override;
};
