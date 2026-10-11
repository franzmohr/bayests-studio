#include "engine/QtReporter.h"

#include <utility>

namespace studio
{

void QtReporter::reset()
{
    cancel_.store(false);
    last_ = {};
    rows_.store(0);
    const std::lock_guard lock(mutex_);
    pending_.clear();
}

void QtReporter::watch(int row)
{
    const std::lock_guard lock(mutex_);
    watch_.store(row);
    pending_.clear();
}

std::vector<std::vector<double>> QtReporter::takeDraws()
{
    const std::lock_guard lock(mutex_);
    return std::exchange(pending_, {});
}

void QtReporter::kept_draw(const std::string &block, long long, const arma::mat &value)
{
    if (block != "a")
    {
        return;
    }
    rows_.store(static_cast<int>(value.n_rows));
    const int row = watch_.load();
    if (row < 0 || static_cast<arma::uword>(row) >= value.n_rows)
    {
        return;
    }
    // One row of the path is copied; the rest of the draw is never touched.
    std::vector<double> path(value.n_cols);
    for (arma::uword t = 0; t < value.n_cols; ++t)
    {
        path[t] = value(static_cast<arma::uword>(row), t);
    }
    const std::lock_guard lock(mutex_);
    // watch() may have switched rows while this one was being copied.
    if (watch_.load() == row)
    {
        pending_.push_back(std::move(path));
    }
}

void QtReporter::message(const std::string &text)
{
    emit messageEmitted(QString::fromStdString(text));
}

void QtReporter::progress(long long done, long long total)
{
    using namespace std::chrono_literals;
    const auto now = std::chrono::steady_clock::now();
    if (done < total && now - last_ < 50ms)
    {
        return;
    }
    last_ = now;
    emit progressChanged(done, total);
}

void QtReporter::finish()
{
    emit finished();
}

void QtReporter::check_interrupt()
{
    if (cancel_.load())
    {
        throw RunCancelled();
    }
}

} // namespace studio
