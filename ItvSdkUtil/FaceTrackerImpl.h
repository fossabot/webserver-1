#ifndef _FACETRACKERIMPL_H_
#define _FACETRACKERIMPL_H_

#include "FaceTracker.h"
#include <deque>
#include <set>

typedef std::pair<ITV8::timestamp_t, ITV8::RectangleF> trackInfo_t;

class CFaceTracker:	public IFaceTracker
{
private:
    typedef std::vector<ITV8::RectangleF> rectanglesList_t;
    typedef rectanglesList_t::size_type rectangleIndex_t;

	//время, через которое трек считается потерянным 
	// в миллисекундах
	static const ITV8::uint64_t m_timeoutTime = 1500;

	// расстояние, с которого начинает создаваться новый трек, а не приписываться к уже имеющимся
	const ITV8::double_t m_distNewTrack;

	// Коэффициент близости к границе кадра, который используется для удаления трека.
	// Считаем, если трек находится от границы, ближе, чем m_coeffNearness*его_средний_размер и 
	// информация по этому треку не менялась на данном кадре, то завершаем трек.
	const ITV8::double_t m_coeffNearness;

	// ширина и высота передаваемых кадров
	ITV8::int32_t m_frameWidth, m_frameHeight;

	// хранилище треков со всеми координатами, существующих к данному моменту
	// хранятся в инвертированном порядке - самый последний трек, храниться в начале
	std::map<ITV8::uint64_t, std::deque<trackInfo_t> > m_allTracks;	

    // хранилище появившихся треков, огранизованное по аналогии с m_allTracks
    std::map<ITV8::uint64_t, std::deque<trackInfo_t> > m_appTracks;	

    // хранилище исчезнувших треков, огранизованное по аналогии с m_allTracks
    std::map<ITV8::uint64_t, std::deque<trackInfo_t> > m_disappTracks;	

	// последний хранящийся ID, изначально он 0
	ITV8::uint64_t m_lastID;

	// функция нахождения центральной точки прямогульника
	ITV8::PointF CalcRectCenter(const ITV8::RectangleF& r);

	// функция вычисления расстояния между двумя точками
	ITV8::double_t CalcDistBetweenPoints(const ITV8::PointF& p1, const ITV8::PointF& p2);

	// функция вычисления косинуса угла между векорами (p1, p2) и (p2, p3)
	ITV8::double_t CalcCosAngleBetweenVectors(const ITV8::PointF& p1, const ITV8::PointF &p2, const ITV8::PointF &p3);

	// функция определения расстояния между прямоугольниками
	ITV8::double_t CalcDistBetweenRects(const ITV8::RectangleF& r1, const ITV8::RectangleF& r2);

	// функция удаления из вектора прямоугольников близких прямоугольников
	void RemoveCloseRects(std::vector<ITV8::RectangleF>& rects);

	// функция предсказания положения объекта по 2м точкам в момент времени tx
	// время передается в порядке неубывания
	ITV8::PointF PredictPosition(const ITV8::timestamp_t& t1, const ITV8::PointF& p1, 
								const ITV8::timestamp_t& t2, const ITV8::PointF& p2,
								const ITV8::timestamp_t& tx);

	// функция предсказания положения объекта по 3м точкам в момент времени tx
	// время передается в порядке неубывания
	ITV8::PointF PredictPosition(const ITV8::timestamp_t& t1, const ITV8::PointF& p1, 
								const ITV8::timestamp_t& t2, const ITV8::PointF& p2,
								const ITV8::timestamp_t& t3, const ITV8::PointF& p3,
								const ITV8::timestamp_t& tx);

	// Функция вычисления диспресии, для отсеивания "дрожащих" объектов
	void getDispersion(std::deque<trackInfo_t> &info, double &dispX, double &dispY);

	// Функция наименьших квадратов для восстановления траектории объекта
	void leastSquareFunction(std::deque<trackInfo_t> &info, 
						     ITV8::double_t &x0, ITV8::double_t &vx, ITV8::double_t &ax,
							 ITV8::double_t &y0, ITV8::double_t &vy, ITV8::double_t &ay);

    typedef std::multimap<ITV8::double_t, std::pair<ITV8::uint64_t, rectangleIndex_t> > distancesMap_t;
    typedef std::set<rectangleIndex_t> rectangleIndexesSet_t;

	// функция соотношения прямоугольников с траекториям на основе карты расстояний
    void FillTrajectsMap(distancesMap_t& distancesMap,
                         rectangleIndexesSet_t& rectsSet, std::set<ITV8::uint64_t>& trajectsSet,
						 const std::vector<ITV8::RectangleF>& rects, const ITV8::timestamp_t& frameTime,
						 std::map<ITV8::uint64_t, std::deque<trackInfo_t> >& allTracks);

	// функция удаления треков из указанного набора, информация о которых не обновлялась длительное время
	void DeleteOldTracks(const ITV8::timestamp_t& frameTime, std::set<ITV8::uint64_t>& trajectsSet,
						 const std::map<ITV8::uint64_t, ITV8::double_t>& borderDistances);

	// функция создания новоых треков по набору номеров прямоугольников
    void CreateNewTracks(const ITV8::timestamp_t& frameTime, std::set<rectangleIndex_t>& rectsSet,
						const std::vector<ITV8::RectangleF>& rects);

    //// функция оценивания близости двух прямоугольников
    //bool closenessEval(ITV8::RectangleF &r1, ITV8::RectangleF &r2);

public:
	CFaceTracker();
	// Время передается в миллисекундах
	void OnFrame(const ITV8::timestamp_t& frameTime, const std::vector<ITV8::RectangleF>& rects);
	void GetCurrentTracks(std::map<ITV8::uint64_t, ITV8::RectangleF>& appearedTracks,
                          std::map<ITV8::uint64_t, ITV8::RectangleF>& currentTracks,
                          std::map<ITV8::uint64_t, ITV8::RectangleF>& disappearedTracks);
	void ForceFinishTracks(std::map<ITV8::uint64_t, ITV8::RectangleF>& disappearedTracks);

	~CFaceTracker();
};

IFaceTracker* CreateTracker()
{
	return new CFaceTracker();
}

#endif