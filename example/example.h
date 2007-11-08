#include <QtGui/QtGui>

class Example : QObject
{
    Q_OBJECT

signals:
    void done();

public:
    bool isDone();
    void launch(int value);

protected:
    bool crashed();

};

