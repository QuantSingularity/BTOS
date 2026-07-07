#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

struct sqlite3;

using namespace std;

namespace btos {

struct ExperimentRecord {
    string run_id;
    string config_hash;
    string dataset_hash;
    string git_commit;
    uint64_t seed{0};
    string metrics_json;
    string created_utc;
};

class IExperimentStore {
  public:
    virtual ~IExperimentStore() = default;

    virtual void insert(const ExperimentRecord& rec) = 0;

    [[nodiscard]] virtual optional<ExperimentRecord> get(const string& run_id) const = 0;

    [[nodiscard]] virtual vector<ExperimentRecord> recent(size_t limit) const = 0;
};

class JsonlExperimentStore final : public IExperimentStore {
  public:
    explicit JsonlExperimentStore(string path);

    void insert(const ExperimentRecord& rec) override;

    [[nodiscard]] optional<ExperimentRecord> get(const string& run_id) const override;

    [[nodiscard]] vector<ExperimentRecord> recent(size_t limit) const override;

  private:
    [[nodiscard]] vector<ExperimentRecord> load_all() const;

    string path_;
};

class SqliteExperimentStore final : public IExperimentStore {
  public:
    explicit SqliteExperimentStore(const string& path);
    ~SqliteExperimentStore() override;
    SqliteExperimentStore(const SqliteExperimentStore&) = delete;
    SqliteExperimentStore& operator=(const SqliteExperimentStore&) = delete;

    void insert(const ExperimentRecord& rec) override;

    [[nodiscard]] optional<ExperimentRecord> get(const string& run_id) const override;

    [[nodiscard]] vector<ExperimentRecord> recent(size_t limit) const override;

  private:
    sqlite3* db_{nullptr};
};

unique_ptr<IExperimentStore> open_experiment_store(const string& path);

string current_git_commit(const string& dir);

}
