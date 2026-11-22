#include "solver.h"
#include <QFile>
#include <QTemporaryFile>
#include <QtMath>
#include <algorithm>
#include <proj.h>
#include "rtklib.h"

struct Solver::State {
    raw_t raw{};
    rtcm_t base{};
    rtk_t filter{};
    gtime_t last{};
    bool rawOk = init_raw(&raw, STRFMT_RT17), baseOk = false;
    int station = -1;
    double stationPos[3]{};
    ~State() {
        if (rawOk)
            free_raw(&raw);
        if (baseOk)
            free_rtcm(&base);
        rtkfree(&filter);
    }
};
Solver::Solver() : s(new State) { resetBase(); }
Solver::~Solver() = default;
bool Solver::ready() const { return s->rawOk && s->baseOk; }
void Solver::setReplayTime(int week, int tow) {
    s->base.time = gpst2time(week, tow);
    snprintf(s->raw.opt, sizeof(s->raw.opt), "-WEEK=%d", week);
}
void Solver::resetBase() {
    if (s->baseOk)
        free_rtcm(&s->base);
    memset(&s->base, 0, sizeof(s->base));
    s->baseOk = init_rtcm(&s->base);
    s->station = -1;
    std::fill_n(s->stationPos, 3, 0.0);
    rtkfree(&s->filter);
    auto opt = prcopt_default;
    opt.mode = PMODE_KINEMA;
    opt.navsys = SYS_GPS;
    opt.nf = 2;
    opt.outsingle = 1;
    opt.maxtdiff = 5;
    rtkinit(&s->filter, &opt);
}
int Solver::base(const QByteArray &bytes) {
    int result = 0;
    for (unsigned char c : bytes) {
        int r = input_rtcm3(&s->base, c);
        if (r != 1 && r != 5)
            continue;
        bool moved = r == 5 && norm(s->stationPos, 3) > 0 &&
                     !std::equal(s->stationPos, s->stationPos + 3, s->base.sta.pos);
        if (s->station != -1 && (s->station != s->base.staid || moved)) {
            // Never combine new-station observations with the old coordinate or ambiguity filter.
            QByteArray packet(reinterpret_cast<char *>(s->base.buff), s->base.len + 3);
            resetBase();
            for (unsigned char b : packet)
                r = input_rtcm3(&s->base, b);
        }
        s->station = s->base.staid;
        if (r == 5)
            std::copy_n(s->base.sta.pos, 3, s->stationPos);
        if (r == 1)
            result = 1;
    }
    return result;
}
QVariantMap Solver::rover(const QByteArray &bytes) {
    for (unsigned char c : bytes) {
        if (input_rt17(&s->raw, c) != 1 || !s->raw.obs.n)
            continue;
        obsd_t obs[2 * MAXOBS];
        int n = 0, b = 0;
        auto t = s->raw.obs.data[0].time;
        if (timediff(t, s->last) <= 0)
            continue;
        s->last = t;
        for (int i = 0; i < s->raw.obs.n && n < MAXOBS; i++) {
            obs[n] = s->raw.obs.data[i];
            obs[n++].rcv = 1;
        }
        auto &base = s->base;
        if (norm(base.sta.pos, 3) > 6e6 && norm(base.sta.pos, 3) < 7e6) {
            matcpy(s->filter.opt.rb, base.sta.pos, 3, 1);
            for (int i = 0; i < base.obs.n && b < MAXOBS; i++) {
                auto o = base.obs.data[i];
                if (satsys(o.sat, nullptr) != SYS_GPS || fabs(timediff(t, o.time)) > 5)
                    continue;
                o.rcv = 2;
                obs[n + b++] = o;
            }
        }
        auto cmp = [](const obsd_t &x, const obsd_t &y) { return x.sat < y.sat; };
        std::sort(obs, obs + n, cmp);
        std::sort(obs + n, obs + n + b, cmp);
        int week;
        double tow = time2gpst(t, &week);
        char gpst[48];
        time2str(t, gpst, 3);
        QVariantMap p{{"gpst", QString::fromLatin1(gpst)},
                      {"epochMs", qRound64((week * 604800.0 + tow) * 1000)},
                      {"roverSats", n},
                      {"baseSats", b},
                      {"quality", "none"},
                      {"station", base.staid}};
        if (b) {
            p["baseX"] = base.sta.pos[0];
            p["baseY"] = base.sta.pos[1];
            p["baseZ"] = base.sta.pos[2];
        }
        auto &sol = s->filter.sol;
        if (!rtkpos(&s->filter, obs, n + b, &s->raw.nav) || !sol.stat)
            return p;
        p["quality"] = sol.stat == SOLQ_FIX      ? "fixed"
                       : sol.stat == SOLQ_FLOAT  ? "float"
                       : sol.stat == SOLQ_SINGLE ? "standalone"
                       : sol.stat == SOLQ_DGPS   ? "dgps"
                                                 : "other";
        p["used"] = sol.ns;
        if (sol.stat == SOLQ_FIX || sol.stat == SOLQ_FLOAT) {
            p["ageS"] = sol.age;
            p["ratio"] = sol.ratio;
        }
        double llh[3];
        ecef2pos(sol.rr, llh);
        p["latitudeDeg"] = llh[0] * R2D;
        p["longitudeDeg"] = llh[1] * R2D;
        p["ellipsoidM"] = llh[2];
        p["x"] = sol.rr[0];
        p["y"] = sol.rr[1];
        p["z"] = sol.rr[2];
        return p;
    }
    return {};
}
QVariantMap average(const QVariantList &samples) {
    if (samples.isEmpty())
        return {};
    double mean[3]{}, llh[3], h2 = 0, v2 = 0;
    const char *keys[] = {"x", "y", "z"};
    int fixed = 0;
    for (const auto &sample : samples) {
        auto p = sample.toMap();
        for (int i = 0; i < 3; i++)
            mean[i] += p.value(keys[i]).toDouble();
        fixed += p["quality"] == "fixed";
    }
    for (double &v : mean)
        v /= samples.size();
    ecef2pos(mean, llh);
    for (const auto &sample : samples) {
        double d[3], enu[3];
        auto p = sample.toMap();
        for (int i = 0; i < 3; i++)
            d[i] = p.value(keys[i]).toDouble() - mean[i];
        ecef2enu(llh, d, enu);
        h2 += enu[0] * enu[0] + enu[1] * enu[1];
        v2 += enu[2] * enu[2];
    }
    return {{"latitudeDeg", llh[0] * R2D},
            {"longitudeDeg", llh[1] * R2D},
            {"ellipsoidM", llh[2]},
            {"horizontalRmsM", sqrt(h2 / samples.size())},
            {"verticalRmsM", sqrt(v2 / samples.size())},
            {"epochs", samples.size()},
            {"fixedEpochs", fixed},
            {"quality", fixed == samples.size() ? "fixed"
                        : fixed                 ? "mixed float/fixed"
                                                : "float"}};
}
void addHeights(QVariantMap &p, double pole) {
    // The checked grid is a resource; PROJ requires a native file path.
    static std::unique_ptr<QTemporaryFile> file(
        QTemporaryFile::createNativeFile(":/ca_nrc_CGG2013an83.tif"));
    static auto grid = std::unique_ptr<PJ, decltype(&proj_destroy)>(
        file ? proj_create(nullptr, ("+proj=vgridshift +multiplier=1 +grids=\"" + file->fileName() + "\"")
                                        .toUtf8()
                                        .constData())
             : nullptr,
        proj_destroy);
    p["poleM"] = pole;
    p["groundEllipsoidM"] = p["ellipsoidM"].toDouble() - pole;
    if (!grid)
        return;
    proj_errno_reset(grid.get());
    double h = proj_trans(grid.get(), PJ_INV,
                          proj_coord(p["longitudeDeg"].toDouble() * D2R, p["latitudeDeg"].toDouble() * D2R,
                                     p["ellipsoidM"].toDouble(), 0))
                   .xyz.z;
    if (!proj_errno(grid.get()) && std::isfinite(h)) {
        p["geoid"] = "CGG2013a / NAD83(CSRS)v6 assumed";
        p["antennaCgvd2013M"] = h;
        p["groundCgvd2013M"] = h - pole;
    }
}
