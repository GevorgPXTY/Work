/*
Есть несколько заводов, каждый из которых выпускает продукцию одного типа. Например, завод A выпускает продукт "a", 
завод B - продукт "b" и т.д. Каждый завод выпускает разное количество своей продукции. Завод А - n единиц в час, 
B - 1.1n единиц в 1 час, С - 1.2n единиц продукции в час и т.д. Размерами продукции пренебрегаем и предполагаем 
одинаковыми для всех фабрик, однако каждый продукт имеет уникальные свойства: название, вес, тип упаковки.

Необходимо организовать эффективное складирование продукции заводов, а так же доставку в торговые сети из расчёта, 
что склад может вмещать M * (сумму продукции всех фабрик в час). По заполнению склада не менее чем на 95% склад 
должен начинать освобождаться от продукции следующим образом. Продукцию со склада забирает грузовой транспорт 
различной вместимости (не менее двух видов грузовиков). Грузовик может забирать продукцию разных типов.

М может быть переменным, но не менее 100. Число заводов может быть переменным, но не менее трёх. n может быть переменным, но не менее 50

Необходимо вывести следующие результаты работы алгоритма:

- последовательность поступления продукции на склад (фабрика, продукт, число единиц)

- необходимо подсчитать статистику, сколько продукции и какого состава в среднем перевозят грузовики.

Приложение предлагается реализовать многопоточным.
*/

#include <iostream>
#include <thread>
#include <vector>
#include <mutex>
#include <condition_variable>
#include <unordered_map>
#include <chrono>
#include <string>
#include <algorithm>
#include <locale>



// Структура для представления продукта
struct Product {
    std::string name;          // Название продукта
    int quantity;              // Количество произведённых единиц
    double weight;             // Вес партии продукта
    std::string packagingType; // Тип упаковки
};

// Класс фабрики, которая производит продукцию
class Factory {
public:
    // Конструктор инициализирует имя фабрики, коэффициент производства и базовый уровень
    Factory(std::string name, double production_rate, int base_rate)
        : name_(name), production_rate_(production_rate), base_rate_(base_rate) {}

    // Метод для производства продукции
    Product produce() {
        int produced_quantity = static_cast<int>(base_rate_ * production_rate_); // Расчёт количества продукции
        double weight = produced_quantity * 1.5; // Примерный расчёт веса
        std::string packagingType = "Стандартная упаковка"; // Упаковка по умолчанию
        return {name_, produced_quantity, weight, packagingType}; // Возвращаем объект Product
    }

private:
    std::string name_;            // Название продукта
    double production_rate_;      // Коэффициент производства
    int base_rate_;               // Базовый уровень производства
};

// Класс склада для хранения продукции и отгрузки грузовиками
class Warehouse {
public:
    // Конструктор для инициализации склада
    Warehouse(int max_capacity, int max_trucks = -1)
        : max_capacity_(max_capacity), current_capacity_(0), max_trucks_(max_trucks), trucks_loaded_(0), is_finished_(false) {}

    // Метод для добавления продукции на склад
    void storeProduct(const Product& product) {
        std::unique_lock<std::mutex> lock(mutex_); // Блокировка для потокобезопасности
        if (current_capacity_ + product.quantity > max_capacity_) { // Если склад переполнен
            condition_.wait(lock, [this]() { return current_capacity_ <= max_capacity_ * 0.95 || is_finished_; }); // Ожидание освобождения
            if (is_finished_) return; // Если достигнут лимит грузовиков, завершение метода
        }

        // Добавление продукции на склад
        storage_[product.name] += product.quantity;
        current_capacity_ += product.quantity;
        std::cout << "Продукт " << product.name << " добавлен на склад. Количество добавлено: " 
                  << product.quantity << ". Общее количество на складе: " << current_capacity_ << "\n";

        if (current_capacity_ >= max_capacity_ * 0.95) {
            condition_.notify_all(); // Уведомление для начала отгрузки
        }
    }

    // Метод для отгрузки продукции грузовиком
    std::unordered_map<std::string, int> loadTruck(int capacity) {
        std::unique_lock<std::mutex> lock(mutex_);
        condition_.wait(lock, [this]() { return current_capacity_ >= max_capacity_ * 0.95 || is_finished_; });

        if (is_finished_) return {};  // Завершить поток, если достигнут лимит грузовиков

        std::cout << "Текущий запас на складе перед загрузкой:\n";
        for (const auto& item : storage_) {
            std::cout << item.first << ": " << item.second << " ед.\n";
        }

        // Создаем вектор для сортировки по количеству продукции
        std::vector<std::pair<std::string, int>> items(storage_.begin(), storage_.end());
        std::sort(items.begin(), items.end(), [](const auto& a, const auto& b) { return a.second > b.second; });

        std::unordered_map<std::string, int> truck_load;
        int loaded_quantity = 0;
        for (const auto& item : items) {
            if (loaded_quantity >= capacity) break;

            int load = std::min(item.second, capacity - loaded_quantity);
            truck_load[item.first] = load;
            storage_[item.first] -= load;
            loaded_quantity += load;
            current_capacity_ -= load;
            cumulative_load_[item.first] += load;
        }

        trucks_loaded_++;
        if (max_trucks_ > 0 && trucks_loaded_ >= max_trucks_) {
            is_finished_ = true;
            condition_.notify_all();
        }

        std::cout << "Грузовик загружен. Объем отгружено: " << loaded_quantity << "\n";
        return truck_load;
    }

    // Метод для вывода средней статистики по загруженности грузовиков
    void printAverageLoadStats() {
        std::cout << "Средняя загрузка грузовиков:\n";
        for (const auto& [product, quantity] : cumulative_load_) {
            double average = static_cast<double>(quantity) / trucks_loaded_;
            std::cout << "- " << product << ": " << average << " ед. в среднем на грузовик\n";
        }
        std::cout << "Работа завершена: было отгружено " << trucks_loaded_ << " грузовиков.\n";
    }

    // Проверка завершения работы склада
    bool isFinished() const {
        return is_finished_;
    }

    int getTrucksLoaded() const { return trucks_loaded_; }

private:
    std::unordered_map<std::string, int> storage_; // Хранение продукции по имени
    int max_capacity_;                             // Максимальная ёмкость склада
    int current_capacity_;                         // Текущая заполненность склада
    mutable std::mutex mutex_;                     // Мьютекс для синхронизации
    std::condition_variable condition_;            // Условная переменная для ожидания
    std::unordered_map<std::string, int> cumulative_load_; // Учёт произведённой отгрузки
    int max_trucks_;                               // Лимит по количеству грузовиков
    int trucks_loaded_;                            // Счётчик отгруженных грузовиков
    bool is_finished_;                             // Флаг завершения работы
};

// Поток для работы фабрики
void factoryThreadFunction(Factory& factory, Warehouse& warehouse) {
    while (!warehouse.isFinished()) {
        Product product = factory.produce();
        warehouse.storeProduct(product);
        std::this_thread::sleep_for(std::chrono::milliseconds(100)); // Производство каждые 0.1 секунды
    }
}

// Поток для отгрузки грузовиков
void truckThreadFunction(Warehouse& warehouse, int truck_capacity) {
    while (!warehouse.isFinished()) {
        auto load = warehouse.loadTruck(truck_capacity);
        if (load.empty()) break;
        std::cout << "Грузовик загружен:\n";
        for (const auto& [product, quantity] : load) {
            std::cout << "- " << product << ": " << quantity << " ед.\n";
        }
        std::cout << "Всего загружено: " << truck_capacity << " ед.\n";
        std::this_thread::sleep_for(std::chrono::milliseconds(100)); // Время на разгрузку
    }
}

int main() {
    setlocale(LC_ALL, "Russian");
    int base_rate = 50;
    double M = 100.0;
    std::vector<int> truck_capacities = {500, 300};
    int max_trucks;
    std::cout << "Введите количество грузовиков (-1 для бесконечного режима): ";
    std::cin >> max_trucks;

    std::vector<Factory> factories = {
        Factory("a", 1.0, base_rate),
        Factory("b", 1.1, base_rate),
        Factory("c", 1.2, base_rate)
    };

    int total_production_per_hour = 0;
    for (auto& factory : factories) {
        total_production_per_hour += factory.produce().quantity;
    }
    int warehouse_capacity = static_cast<int>(total_production_per_hour * M);

    Warehouse warehouse(warehouse_capacity, max_trucks);

    std::vector<std::thread> factoryThreads;
    for (auto& factory : factories) {
        factoryThreads.emplace_back(factoryThreadFunction, std::ref(factory), std::ref(warehouse));
    }

    std::vector<std::thread> truckThreads;
    for (int capacity : truck_capacities) {
        truckThreads.emplace_back(truckThreadFunction, std::ref(warehouse), capacity);
    }

    for (auto& t : factoryThreads) t.join();
    for (auto& t : truckThreads) t.join();

    warehouse.printAverageLoadStats();
    return 0;
}

