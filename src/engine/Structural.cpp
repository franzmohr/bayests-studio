#include "engine/Structural.h"

#include "engine/ModelFileTree.h"
#include "engine/ModelRunner.h"

#include <QObject>

#include <highfive/highfive.hpp>

#include <cmath>
#include <optional>
#include <stdexcept>

namespace studio
{

QString responseTypeName(ResponseType type)
{
    switch (type)
    {
    case ResponseType::Feir: return QObject::tr("Forecast error (feir)");
    case ResponseType::Oir: return QObject::tr("Orthogonalised (oir)");
    case ResponseType::Gir: return QObject::tr("Generalised (gir)");
    case ResponseType::Sir: return QObject::tr("Structural (sir)");
    case ResponseType::Sgir: return QObject::tr("Structural generalised (sgir)");
    }
    return {};
}

namespace
{

/// One draw's system at the chosen period: the reduced form lag matrices
/// [A_1 .. A_p] (k x kp), and Sigma -- the reduced form error covariance, or
/// that of the structural errors for a structural model, whose A_0 comes too.
struct System
{
    arma::mat A;
    arma::mat Sigma;
    arma::mat A0;   ///< empty unless structural
};

/// The draws of a model file, and how to cut one system out of them.
class Posterior
{
public:
    explicit Posterior(const QString &file)
    {
        const CheckReport report = ModelRunner::check(file, QString());
        if (!report.ok)
        {
            throw std::runtime_error(report.error.toStdString());
        }
        if (!report.hasPosterior)
        {
            throw std::runtime_error(QObject::tr("The model has no posterior draws yet.").toStdString());
        }
        algorithm_ = report.algorithm;
        if (report.factors > 0 || algorithm_.startsWith(QLatin1String("Uc")))
        {
            throw std::runtime_error(
                QObject::tr("Impulse responses of factor and unobserved components models are not "
                            "available yet.").toStdString());
        }
        if (algorithm_.endsWith(QLatin1String("Ald")))
        {
            // The quantile VARs estimate no error covariance: their
            // u_sigma_inv holds the latent mixture precisions (bvartools'
            // .refuse_quantile_covariance()).
            throw std::runtime_error(
                QObject::tr("A quantile VAR estimates no error covariance to identify shocks from.").toStdString());
        }
        if (algorithm_.endsWith(QLatin1String("Discount")))
        {
            throw std::runtime_error(
                QObject::tr("The discounted models write a posterior per period, not draws; "
                            "impulse responses from them are not available yet.").toStdString());
        }
        k_ = report.k;
        p_ = report.p;
        rank_ = report.rank;
        kBeta_ = report.kBeta;
        structural_ = report.structural;
        tt_ = report.periods;
        vec_ = algorithm_.startsWith(QLatin1String("Vec"));
        a_ = readMatrix(file, QStringLiteral("/posterior/a/coeffs"));
        sigmaInv_ = readMatrix(file, QStringLiteral("/posterior/u_sigma_inv/coeffs"));
        if (vec_)
        {
            beta_ = readMatrix(file, QStringLiteral("/posterior/beta/coeffs"));
        }
        const auto kk = static_cast<std::size_t>(k_ * k_);
        sigmaMoves_ = sigmaInv_.rows == kk * static_cast<std::size_t>(tt_) && tt_ > 1;
        aMoves_ = algorithm_.contains(QLatin1String("Tvp"));
        nparams_ = static_cast<int>(aMoves_ ? a_.rows / static_cast<std::size_t>(tt_) : a_.rows);
    }

    int draws() const { return static_cast<int>(a_.cols); }
    int k() const { return k_; }
    bool structural() const { return structural_; }
    bool moving() const { return aMoves_ || sigmaMoves_; }
    int periods() const { return tt_; }

    System system(int draw, int period) const
    {
        const auto d = static_cast<std::size_t>(draw);
        const auto k = static_cast<arma::uword>(k_);
        const int t = (period < 1 || period > tt_) ? tt_ : period;
        const std::size_t aOff = aMoves_ ? static_cast<std::size_t>(t - 1) * nparams_ : 0;
        const auto coef = [&](std::size_t i) { return a_.at(aOff + i, d); };

        System s;
        const int nStruct = structural_ ? k_ * (k_ - 1) / 2 : 0;
        if (structural_)
        {
            // Unit lower triangular, the free elements column by column:
            // (2,1), (3,1), ..., (k,1), (3,2), ... -- the order of the
            // contemporaneous regressors (bvartools' collect_draws.R).
            s.A0 = arma::eye<arma::mat>(k, k);
            std::size_t pos = static_cast<std::size_t>(nparams_ - nStruct);
            for (arma::uword j = 0; j < k; ++j)
            {
                for (arma::uword i = j + 1; i < k; ++i)
                {
                    s.A0(i, j) = coef(pos++);
                }
            }
        }

        // The lag polynomial, less the identity a VEC's level form adds back.
        const int levelLags = std::max(p_, 1);
        arma::mat lags(k, k * static_cast<arma::uword>(levelLags), arma::fill::zeros);
        if (!vec_)
        {
            for (std::size_t i = 0; i < static_cast<std::size_t>(k_ * k_ * p_); ++i)
            {
                lags(i % k, i / k) = coef(i);
            }
        }
        else
        {
            // a = vec(alpha) (k x r), then vec([Gamma_1 .. Gamma_{p-1}, ...]);
            // beta's rows are y_{t-1} and then the restricted terms, which
            // the impulse response does not see.
            const auto r = static_cast<arma::uword>(rank_);
            arma::mat alpha(k, r);
            for (std::size_t i = 0; i < k * r; ++i)
            {
                alpha(i % k, i / k) = coef(i);
            }
            const std::size_t nBeta = static_cast<std::size_t>(kBeta_) * r;
            const bool betaMoves = beta_.rows == nBeta * static_cast<std::size_t>(tt_) && tt_ > 1;
            const std::size_t bOff = betaMoves ? static_cast<std::size_t>(t - 1) * nBeta : 0;
            arma::mat beta(static_cast<arma::uword>(kBeta_), r);
            for (std::size_t i = 0; i < nBeta; ++i)
            {
                beta(i % static_cast<std::size_t>(kBeta_), i / static_cast<std::size_t>(kBeta_)) =
                    beta_.at(bOff + i, d);
            }
            const arma::mat pi = alpha * arma::trans(beta.rows(0, k - 1));
            std::vector<arma::mat> gamma;
            const std::size_t gOff = k * r;
            for (int g = 0; g < p_ - 1; ++g)
            {
                arma::mat G(k, k);
                for (std::size_t i = 0; i < k * k; ++i)
                {
                    G(i % k, i / k) = coef(gOff + static_cast<std::size_t>(g) * k * k + i);
                }
                gamma.push_back(G);
            }
            // A_1 = I + Pi + Gamma_1, A_i = Gamma_i - Gamma_{i-1}, A_p = -Gamma_{p-1}.
            for (int i = 1; i <= levelLags; ++i)
            {
                arma::mat Ai = (i == 1) ? pi : arma::mat(k, k, arma::fill::zeros);
                if (i - 1 < static_cast<int>(gamma.size()))
                {
                    Ai += gamma[static_cast<std::size_t>(i - 1)];
                }
                if (i >= 2)
                {
                    Ai -= gamma[static_cast<std::size_t>(i - 2)];
                }
                lags.cols(static_cast<arma::uword>((i - 1) * k_), static_cast<arma::uword>(i * k_ - 1)) = Ai;
            }
        }
        if (structural_)
        {
            lags = arma::solve(s.A0, lags);
        }
        if (vec_)
        {
            lags.cols(0, k - 1) += arma::eye<arma::mat>(k, k);
        }
        s.A = lags;

        const std::size_t kk = k * k;
        const std::size_t sOff = sigmaMoves_ ? static_cast<std::size_t>(t - 1) * kk : 0;
        arma::mat precision(k, k);
        for (std::size_t i = 0; i < kk; ++i)
        {
            precision(i % k, i / k) = sigmaInv_.at(sOff + i, d);
        }
        s.Sigma = arma::inv_sympd(arma::symmatu(precision));
        return s;
    }

private:
    QString algorithm_;
    int k_ = 0, p_ = 0, rank_ = 0, kBeta_ = 0, tt_ = 0, nparams_ = 0;
    bool structural_ = false, vec_ = false, aMoves_ = false, sigmaMoves_ = false;
    Matrix a_, sigmaInv_, beta_;
};

/// Phi_0 .. Phi_h of the lag polynomial A (k x kp): Phi_i = sum_j Phi_{i-j} A_j.
std::vector<arma::mat> movingAverage(const arma::mat &A, int horizon)
{
    const arma::uword k = A.n_rows;
    const int p = static_cast<int>(A.n_cols / k);
    std::vector<arma::mat> phi;
    phi.push_back(arma::eye<arma::mat>(k, k));
    for (int i = 1; i <= horizon; ++i)
    {
        arma::mat next(k, k, arma::fill::zeros);
        for (int j = 1; j <= std::min(i, p); ++j)
        {
            next += phi[static_cast<std::size_t>(i - j)] *
                    A.cols(static_cast<arma::uword>((j - 1)) * k, static_cast<arma::uword>(j) * k - 1);
        }
        phi.push_back(next);
    }
    return phi;
}

std::vector<int> drawSubset(int draws, int maxDraws)
{
    std::vector<int> pick;
    const int n = std::min(draws, std::max(1, maxDraws));
    for (int i = 0; i < n; ++i)
    {
        pick.push_back(static_cast<int>((static_cast<long long>(i) * draws) / n));
    }
    return pick;
}

void checkType(const Posterior &post, ResponseType type)
{
    const bool structuralType = type == ResponseType::Sir || type == ResponseType::Sgir;
    if (structuralType && !post.structural())
    {
        throw std::runtime_error(QObject::tr("Structural responses need a structural model.").toStdString());
    }
    if (!structuralType && post.structural())
    {
        // bvartools' irf(): the recursion would run on the structural
        // coefficients and the covariance of the structural errors.
        throw std::runtime_error(
            QObject::tr("A structural model's responses are sir or sgir: the others would be computed "
                        "from the structural coefficients rather than the reduced form.").toStdString());
    }
}

} // namespace

QStringList levelNames(const QString &file)
{
    QStringList names = readStringsAttribute(file, QStringLiteral("/data/train/y"), QStringLiteral("variables"));
    bool differenced = !names.isEmpty();
    for (const QString &n : names)
    {
        differenced = differenced && n.startsWith(QLatin1String("d."));
    }
    if (differenced)
    {
        for (QString &n : names)
        {
            n = n.mid(2);
        }
    }
    return names;
}

StructuralSupport structuralSupport(const QString &file)
{
    StructuralSupport out;
    try
    {
        const Posterior post(file);
        out.ok = true;
        out.structural = post.structural();
        out.moving = post.moving();
        out.periods = post.periods();
        out.k = post.k();
        out.variables = levelNames(file);
        for (int i = static_cast<int>(out.variables.size()); i < out.k; ++i)
        {
            out.variables << QObject::tr("Variable %1").arg(i + 1);
        }
    }
    catch (const std::exception &e)
    {
        out.reason = QString::fromUtf8(e.what());
    }
    return out;
}

std::vector<arma::mat> impulseResponses(const QString &file, const ResponseRequest &request, int impulse)
{
    const Posterior post(file);
    checkType(post, request.type);
    const auto j = static_cast<arma::uword>(impulse);
    std::vector<arma::mat> out;
    for (const int d : drawSubset(post.draws(), request.maxDraws))
    {
        const System s = post.system(d, request.period);
        const arma::uword k = s.A.n_rows;
        arma::mat P = arma::eye<arma::mat>(k, k);
        switch (request.type)
        {
        case ResponseType::Feir: break;
        case ResponseType::Oir: P = arma::trans(arma::chol(s.Sigma)); break;
        case ResponseType::Gir: P = s.Sigma / std::sqrt(s.Sigma(j, j)); break;
        case ResponseType::Sir: P = arma::inv(s.A0); break;
        case ResponseType::Sgir: P = arma::solve(s.A0, s.Sigma) / std::sqrt(s.Sigma(j, j)); break;
        }
        // feir and sir count a shock in units of the error unless asked for
        // standard deviations, as bvartools' shock = "sd".
        if (request.standardDeviation && (request.type == ResponseType::Feir || request.type == ResponseType::Sir))
        {
            P *= std::sqrt(s.Sigma(j, j));
        }
        const std::vector<arma::mat> phi = movingAverage(s.A, request.horizon);
        arma::mat response(static_cast<arma::uword>(request.horizon + 1), k);
        for (int h = 0; h <= request.horizon; ++h)
        {
            response.row(static_cast<arma::uword>(h)) = arma::trans(arma::mat(phi[static_cast<std::size_t>(h)] * P).col(j));
        }
        if (request.cumulative)
        {
            response = arma::cumsum(response, 0);
        }
        out.push_back(response);
    }
    return out;
}

std::vector<arma::mat> varianceDecomposition(const QString &file, const ResponseRequest &request, int response)
{
    if (request.type != ResponseType::Oir && request.type != ResponseType::Sir)
    {
        throw std::runtime_error(
            QObject::tr("The variance decomposition is offered for orthogonal shocks (oir, sir), whose "
                        "shares sum to one.").toStdString());
    }
    const Posterior post(file);
    checkType(post, request.type);
    const auto i = static_cast<arma::uword>(response);
    std::vector<arma::mat> out;
    for (const int d : drawSubset(post.draws(), request.maxDraws))
    {
        const System s = post.system(d, request.period);
        const arma::uword k = s.A.n_rows;
        // bvartools' vardecomp(): P = chol(Sigma)' for oir, A_0^-1 chol(Sigma)'
        // for sir, with Sigma the structural errors' (diagonal) covariance.
        arma::mat P = arma::trans(arma::chol(s.Sigma));
        if (request.type == ResponseType::Sir)
        {
            P = arma::solve(s.A0, P);
        }
        const std::vector<arma::mat> phi = movingAverage(s.A, request.horizon);
        arma::mat shares(static_cast<arma::uword>(request.horizon + 1), k);
        arma::rowvec numerator(k, arma::fill::zeros);
        for (int h = 0; h <= request.horizon; ++h)
        {
            numerator += arma::square(arma::mat(phi[static_cast<std::size_t>(h)] * P).row(i));
            shares.row(static_cast<arma::uword>(h)) = numerator / arma::accu(numerator);
        }
        out.push_back(shares);
    }
    return out;
}

} // namespace studio
