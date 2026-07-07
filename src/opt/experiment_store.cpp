#include "btos/opt/experiment_store.hpp"

#include <sqlite3.h>

#include <array>
#include <cstdio>
#include <fstream>
#include <memory>
#include <stdexcept>

#include <nlohmann/json.hpp>

using namespace std;
using json = nlohmann::json;

namespace btos {

JsonlExperimentStore::JsonlExperimentStore(string path) : path_(std::move(path)) {}

void JsonlExperimentStore::insert(const ExperimentRecord& rec) {
    json j{{"run_id", rec.run_id},
           {"config_hash", rec.config_hash},
           {"dataset_hash", rec.dataset_hash},
           {"git_commit", rec.git_commit},
           {"seed", rec.seed},
           {"metrics", rec.metrics_json},
           {"created_utc", rec.created_utc}};
    ofstream out(path_, ios::app);
    if (!out) throw runtime_error("experiment store: cannot open " + path_);
    out << j.dump() << "\n";
    if (!out) throw runtime_error("experiment store: write failed for " + path_);
}

vector<ExperimentRecord> JsonlExperimentStore::load_all() const {
    vector<ExperimentRecord> out;
    ifstream in(path_);
    if (!in) return out;
    string line;
    while (getline(in, line)) {
        if (line.empty()) continue;
        json j = json::parse(line, nullptr, false);
        if (j.is_discarded()) continue;
        ExperimentRecord r;
        r.run_id = j.value("run_id", "");
        r.config_hash = j.value("config_hash", "");
        r.dataset_hash = j.value("dataset_hash", "");
        r.git_commit = j.value("git_commit", "");
        r.seed = j.value("seed", uint64_t{0});
        r.metrics_json = j.value("metrics", "");
        r.created_utc = j.value("created_utc", "");
        out.push_back(std::move(r));
    }
    return out;
}

optional<ExperimentRecord> JsonlExperimentStore::get(const string& run_id) const {
    optional<ExperimentRecord> found;
    for (auto& r : load_all())
        if (r.run_id == run_id) found = std::move(r);
    return found;
}

vector<ExperimentRecord> JsonlExperimentStore::recent(size_t limit) const {
    vector<ExperimentRecord> all = load_all();
    if (all.size() > limit) all.erase(all.begin(), all.end() - static_cast<ptrdiff_t>(limit));
    return {all.rbegin(), all.rend()};
}

namespace {

void sq_check(int rc, sqlite3* db, const char* what) {
    if (rc != SQLITE_OK && rc != SQLITE_DONE && rc != SQLITE_ROW)
        throw runtime_error(string("experiment store: ") + what + ": " +
                            (db != nullptr ? sqlite3_errmsg(db) : "unknown"));
}

struct StmtCloser {
    void operator()(sqlite3_stmt* s) const { sqlite3_finalize(s); }
};
using Stmt = unique_ptr<sqlite3_stmt, StmtCloser>;

Stmt sq_prepare(sqlite3* db, const char* sql) {
    sqlite3_stmt* s = nullptr;
    sq_check(sqlite3_prepare_v2(db, sql, -1, &s, nullptr), db, "prepare");
    return Stmt(s);
}

string sq_text(sqlite3_stmt* s, int i) {
    const auto* p = sqlite3_column_text(s, i);
    return p != nullptr ? reinterpret_cast<const char*>(p) : "";
}

ExperimentRecord sq_row(sqlite3_stmt* s) {
    ExperimentRecord r;
    r.run_id = sq_text(s, 0);
    r.config_hash = sq_text(s, 1);
    r.dataset_hash = sq_text(s, 2);
    r.git_commit = sq_text(s, 3);
    r.seed = static_cast<uint64_t>(sqlite3_column_int64(s, 4));
    r.metrics_json = sq_text(s, 5);
    r.created_utc = sq_text(s, 6);
    return r;
}

}

SqliteExperimentStore::SqliteExperimentStore(const string& path) {
    sq_check(sqlite3_open(path.c_str(), &db_), db_, "open");
    const char* schema =
        "CREATE TABLE IF NOT EXISTS runs ("
        "run_id TEXT PRIMARY KEY,"
        "config_hash TEXT NOT NULL,"
        "dataset_hash TEXT NOT NULL,"
        "git_commit TEXT,"
        "seed INTEGER NOT NULL,"
        "metrics_json TEXT NOT NULL,"
        "created_utc TEXT NOT NULL);";
    char* err = nullptr;
    const int rc = sqlite3_exec(db_, schema, nullptr, nullptr, &err);
    if (rc != SQLITE_OK) {
        string msg = err != nullptr ? err : "unknown";
        sqlite3_free(err);
        throw runtime_error("experiment store: schema: " + msg);
    }
}

SqliteExperimentStore::~SqliteExperimentStore() {
    if (db_ != nullptr) sqlite3_close(db_);
}

void SqliteExperimentStore::insert(const ExperimentRecord& rec) {
    auto s = sq_prepare(db_,
                        "INSERT OR REPLACE INTO runs (run_id, config_hash, dataset_hash, "
                        "git_commit, seed, metrics_json, created_utc) VALUES (?,?,?,?,?,?,?);");
    sqlite3_bind_text(s.get(), 1, rec.run_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(s.get(), 2, rec.config_hash.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(s.get(), 3, rec.dataset_hash.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(s.get(), 4, rec.git_commit.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(s.get(), 5, static_cast<sqlite3_int64>(rec.seed));
    sqlite3_bind_text(s.get(), 6, rec.metrics_json.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(s.get(), 7, rec.created_utc.c_str(), -1, SQLITE_TRANSIENT);
    sq_check(sqlite3_step(s.get()), db_, "insert");
}

optional<ExperimentRecord> SqliteExperimentStore::get(const string& run_id) const {
    auto s = sq_prepare(db_,
                        "SELECT run_id, config_hash, dataset_hash, git_commit, seed, "
                        "metrics_json, created_utc FROM runs WHERE run_id = ?;");
    sqlite3_bind_text(s.get(), 1, run_id.c_str(), -1, SQLITE_TRANSIENT);
    if (sqlite3_step(s.get()) == SQLITE_ROW) return sq_row(s.get());
    return nullopt;
}

vector<ExperimentRecord> SqliteExperimentStore::recent(size_t limit) const {
    auto s = sq_prepare(db_,
                        "SELECT run_id, config_hash, dataset_hash, git_commit, seed, "
                        "metrics_json, created_utc FROM runs ORDER BY created_utc DESC LIMIT ?;");
    sqlite3_bind_int64(s.get(), 1, static_cast<sqlite3_int64>(limit));
    vector<ExperimentRecord> out;
    while (sqlite3_step(s.get()) == SQLITE_ROW) out.push_back(sq_row(s.get()));
    return out;
}

unique_ptr<IExperimentStore> open_experiment_store(const string& path) {
    if (path.size() > 6 && path.substr(path.size() - 6) == ".jsonl")
        return make_unique<JsonlExperimentStore>(path);
    return make_unique<SqliteExperimentStore>(path);
}

string current_git_commit(const string& dir) {
    const string cmd = "git -C '" + dir + "' rev-parse HEAD 2>/dev/null";
    array<char, 128> buf{};
    string out;
    FILE* pipe = popen(cmd.c_str(), "r");
    if (pipe == nullptr) return "";
    while (fgets(buf.data(), static_cast<int>(buf.size()), pipe) != nullptr) out += buf.data();
    pclose(pipe);
    while (!out.empty() && (out.back() == '\n' || out.back() == '\r')) out.pop_back();
    return out;
}

}
