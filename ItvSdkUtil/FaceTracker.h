#ifndef ITVSDKUTIL_FACETRACKER_H
#define ITVSDKUTIL_FACETRACKER_H

#include <ItvSdk/include/baseTypes.h>
#include <vector>
#include <map>

class IFaceTracker
{
public:
    // В функцию передаются рамки распознанного объекта.
    // Должна вызваться на каждом кадре, вне зависимости от того
    // распознано, что то на текущем кадре или нет.
    // rects - список прямоугольников
    // frameTime - текущее время в миллисекундах
    virtual void OnFrame(const ITV8::timestamp_t& frameTime, const std::vector<ITV8::RectangleF>& rects) = 0;    

    // appearedTracks - список появившихся треков с id
    // currentTracks - полный список треков с id
    // disappearedTracks - список исчезнувших треков
    // В качестве результата функция возвращает количество треков, а так же сами треки
    virtual void GetCurrentTracks(std::map<ITV8::uint64_t, ITV8::RectangleF>& appearedTracks,
                                  std::map<ITV8::uint64_t, ITV8::RectangleF>& currentTracks,
                                  std::map<ITV8::uint64_t, ITV8::RectangleF>& disappearedTracks) = 0;

    // Функция "насильного завершения" всех имеющихся треков, и возвращения их в качестве исчезнувших
    virtual void ForceFinishTracks(std::map<ITV8::uint64_t, ITV8::RectangleF>& disappearedTracks) = 0;

    virtual ~IFaceTracker() {}
};

// Функция создающая экземпляр трекера
// В Функцию передаются высота-ширина кадра, на котором происходит треккирование.
IFaceTracker* CreateTracker();

#endif // ITVSDKUTIL_FACETRACKER_H
