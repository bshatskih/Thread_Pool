#pragma once
#include <iostream>
#include <iomanip>
#include <fstream>


struct Logger {

    Logger(std::mutex& logger_mutex) {
        std::lock_guard<std::mutex> lock(logger_mutex);
        std::ofstream file("../log_file.txt");

        file << "Server start working; time: ";
        file << getCurrentTimeFormatted(std::time(nullptr)) << "\n\n";
        file.close();
    }


    std::string getCurrentTimeFormatted(const std::time_t& time) {
        std::tm* tm = std::localtime(&time);

        std::ostringstream oss;
        oss << std::setfill('0') 
            << std::setw(2) << tm->tm_hour << ":"
            << std::setw(2) << tm->tm_min << ":"
            << std::setw(2) << tm->tm_sec << ", "
            << std::setw(2) << tm->tm_mday << "."
            << std::setw(2) << (tm->tm_mon + 1) << "."
            << std::setw(2) << (tm->tm_year % 100);

        return oss.str();
    }


    void add_record_about_task(const std::time_t& start_time, const std::time_t& end_time, const std::string& task_description) {
        std::ofstream file("../log_file.txt", std::ios::app);
        file << "Soleved task with description:\n" << task_description;
        file << "Start working: " << getCurrentTimeFormatted(start_time) << '\n';
        file << "End working: " << getCurrentTimeFormatted(end_time) << '\n';
        file << "Duration: " << (end_time - start_time) << " sec\n\n";
        file.close();
    }


    void log_start(const std::time_t& time) {
        std::ofstream file("../log_file.txt", std::ios::app);
        file << "The server operation has been resumed: " << getCurrentTimeFormatted(time) << "\n\n";
        file.close();
    }


    void log_paused(const std::time_t& time) {
        std::ofstream file("../log_file.txt", std::ios::app);
        file << "The server has been suspended: " << getCurrentTimeFormatted(time) << "\n\n";
        file.close();
    }


    void log_error(const std::time_t& time, const std::string& error_info) {
        std::ofstream file("../log_file.txt", std::ios::app);
        file << "An error has occurred: " << error_info << '\n';
        file << "Time: " << getCurrentTimeFormatted(time) << "\n\n";
        file.close();
    }


    void log_deadlock(const std::time_t& time, size_t count_of_threads) {
        std::ofstream file("../log_file.txt", std::ios::app);
        file << "Deadlock has occurred, a new thread has been created\n" 
             << "Current number of threads - " 
             << count_of_threads << ", new number of threads - " 
             << count_of_threads + 1 << '\n';
        file << "Time: " << getCurrentTimeFormatted(time) << "\n\n";
        file.close();
    }


    ~Logger() {
        std::ofstream file("../log_file.txt", std::ios::app);
        file << "Server end working; time: ";
        file << getCurrentTimeFormatted(std::time(nullptr)) << "\n\n";
        file.close();
    }
};