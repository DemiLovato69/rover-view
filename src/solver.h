#pragma once
#include <QVariantMap>
#include <memory>

class Solver {
    struct State;
    std::unique_ptr<State> s;

  public:
    Solver();
    ~Solver();
    bool ready() const;
    void resetBase();
    void setReplayTime(int week, int tow);
    int base(const QByteArray &bytes);
    QVariantMap rover(const QByteArray &bytes);
};
QVariantMap average(const QVariantList &samples);
void addHeights(QVariantMap &point, double pole);
