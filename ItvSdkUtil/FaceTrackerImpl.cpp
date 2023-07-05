#include "FaceTrackerImpl.h"
#include <iostream>
#include <math.h>
#include <assert.h> 
#include <algorithm>


///////////////////////////////////////////////////////////////////////////////////////////
CFaceTracker::CFaceTracker()
:	m_distNewTrack(0.2)
,	m_coeffNearness(0.1)
,	m_lastID(-1)
{
}

///////////////////////////////////////////////////////////////////////////////////////////
CFaceTracker::~CFaceTracker()
{}

//----------------------------- Геометрические вспомогательные функции --------------------
///////////////////////////////////////////////////////////////////////////////////////////
ITV8::PointF CFaceTracker::CalcRectCenter(const ITV8::RectangleF& r)
{
	return ITV8::PointF(r.left + r.width / 2, r.top + r.height / 2);
}

///////////////////////////////////////////////////////////////////////////////////////////
ITV8::double_t CFaceTracker::CalcDistBetweenPoints(const ITV8::PointF& p1, const ITV8::PointF& p2)
{
	const ITV8::double_t deltaX = p2.x - p1.x;
	const ITV8::double_t deltaY = p2.y - p1.y;

	return sqrt(double(deltaX*deltaX + deltaY*deltaY));
}

/////////////////////////////////////////////////////////////////////////////////////////////
ITV8::double_t CFaceTracker::CalcCosAngleBetweenVectors(const ITV8::PointF& p1, const ITV8::PointF &p2, 
														const ITV8::PointF &p3)
{
	// Угол между векторами вычиляем, как cos(alpha) = (a, b) / (|a| * |b|)
	ITV8::double_t cos = 0.0;
	const ITV8::PointF vect1(p2.x - p1.x, p2.y - p1.y);
	const ITV8::PointF vect2(p3.x - p2.x, p3.y - p2.y);

	const ITV8::double_t len1 = CalcDistBetweenPoints(p1, p2);
	const ITV8::double_t len2 = CalcDistBetweenPoints(p2, p3);
	// проверим, что получились ненулевые векторы
	if (len1 * len2 > 0)
		 cos = (vect1.x * vect2.x + vect1.y * vect2.y) / (len1 * len2); 

	return cos;
}

/////////////////////////////////////////////////////////////////////////////////////////////
ITV8::double_t CFaceTracker::CalcDistBetweenRects(const ITV8::RectangleF &r1, const ITV8::RectangleF &r2)
{
	const ITV8::PointF center1 = CalcRectCenter(r1);
	const ITV8::PointF center2 = CalcRectCenter(r2);
	
	// Расстояние между прямоугольниками считаем, как расстояние между их центрами
	return CalcDistBetweenPoints(center1, center2);
}

/////////////////////////////////////////////////////////////////////////////////////////////
// функция поиска пересечения 2х прямоугольников
ITV8::bool_t FindIntersection(const ITV8::RectangleF& r1, const ITV8::RectangleF& r2, ITV8::RectangleF &res)
{
	// заменяем secondRect пересечением firstRect и secondRect
	ITV8::double_t x1 = std::max<ITV8::double_t>(r1.left, r2.left);
	ITV8::double_t x2 = std::min<ITV8::double_t>(r1.left + r1.width, r2.left + r2.width);
	ITV8::double_t y1 = std::max<ITV8::double_t>(r1.top, r2.top);
	ITV8::double_t y2 = std::min<ITV8::double_t>(r1.top + r1.height, r2.top + r2.height);

	// Если прямоугольник сформирован верно.
	ITV8::bool_t result = x1 < x2 && y1 < y2;
	if (result)
		res = ITV8::RectangleF(x1, y1, x2 - x1, y2 - y1);

	return result;
}

auto closenessEval = [](ITV8::RectangleF const &r1, ITV8::RectangleF const &r2) -> bool
{
    ITV8::RectangleF intersectR;
    if (!FindIntersection(r1, r2, intersectR))
        return false;
    // Если отношение площади пересечения к площади большего прямоугольника больше 0.5,
    // то считаем, что прямоугольники одинаковы
    double aspect = intersectR.width*intersectR.height /
            std::max<ITV8::double_t>(r1.width*r1.height, r2.width*r2.height);
    return aspect > 0.5;
};

/////////////////////////////////////////////////////////////////////////////////////////////
void CFaceTracker::RemoveCloseRects(std::vector<ITV8::RectangleF>& rects)
{
	// Сначала упорядочим прямоугольники по площади
    std::sort(
        rects.begin(),
        rects.end(),
        [](ITV8::RectangleF const &r1, ITV8::RectangleF const &r2) { return r1.width * r1.height < r2.width * r2.height; }
    );
    rects.erase( std::unique(rects.begin(), rects.end(), closenessEval), rects.end() );
}

//----------------------------- Физические вспомогательные функции ------------------------
///////////////////////////////////////////////////////////////////////////////////////////
void CFaceTracker::getDispersion(std::deque<trackInfo_t> &info, double &dispX, double &dispY)
{
	// Посчитаем дисперсию последних N положений номерной пластины
	// Дисперсию будем считать по формуле
	//		D = (sum_1^n(x_i^2) - (sum_1^n(x_i))^2) / (n - 1) 

	// сохраним размер контейнера, чтобы не пересчитывать каждый раз
	const int containerSize = int(info.size());
	if (containerSize <= 1)
	{
		dispX = 0;
		dispY = 0;
		return;
	}
	// Посчитаем индекс первого элемента контейнера, начиная с которого будем расчитывать дисперсию положения
	// для этого найдем номер, пришедший за 2 сек. до последнего
	std::deque<trackInfo_t>::iterator itFirst = info.begin();
	std::deque<trackInfo_t>::iterator itEnd= info.end();
	int Nrects = 0;
	while(itFirst != itEnd)
	{
		// Учли, что время в миллисекундах
		ITV8::uint64_t delta = info.front().first - itFirst->first;
		if (delta > 2000)
		//if (delta > 50)
			break;
		else
		{
			++Nrects;
			++itFirst;
		}
	}
	--itFirst;

	if (Nrects <= 1)
	{
		dispX = 0;
		dispY = 0;
		return;
	}

	double squareOfSumX = 0.0;
	double squareOfSumY = 0.0;
	double sumOfSquareX = 0.0;
	double sumOfSquareY = 0.0;
	while (1)
	{
		double plateCenterX = itFirst->second.left + itFirst->second.width / 2.0;
		double plateCenterY = itFirst->second.top + itFirst->second.height / 2.0; 

		squareOfSumX += plateCenterX;
		sumOfSquareX += (plateCenterX * plateCenterX);

		squareOfSumY += plateCenterY;
		sumOfSquareY += (plateCenterY * plateCenterY);

		if (info.begin() == itFirst)
			break;
		else
			--itFirst;
	}
	
	squareOfSumX *= squareOfSumX;
	squareOfSumY *= squareOfSumY;
	dispX = (sumOfSquareX - squareOfSumX / double(Nrects)) / double(Nrects - 1);
	dispY = (sumOfSquareY - squareOfSumY / double(Nrects)) / double(Nrects - 1);
}
///////////////////////////////////////////////////////////////////////////////////////////
void CFaceTracker::leastSquareFunction(std::deque<trackInfo_t> &info, 
						     ITV8::double_t &x0, ITV8::double_t &vx, ITV8::double_t &ax,
							 ITV8::double_t &y0, ITV8::double_t &vy, ITV8::double_t &ay)
{
	// Подразумевается, что в хранилище не может быть меньше 2х позиций треков
	int infoSize = (int)info.size();
	if (2 == infoSize)
	{
		// Линейно аппроксимируем траекторию
		ITV8::PointF centerStart = CalcRectCenter(info.back().second);
		ITV8::PointF centerEnd = CalcRectCenter(info.front().second);
		ITV8::timestamp_t dt = info.front().first - info.back().first;
		x0 = centerStart.x; vx = (centerEnd.x - centerStart.x) / dt; ax = 0;
		y0 = centerStart.y; vy = (centerEnd.y - centerStart.y) / dt; ay = 0;
	}
	else
	{
		// Задачу решаем следующим образом: sum( x(t_i) - info_i )^2 -> min, что эквивалентно системе
		//	d[sum( x_i - info_i )^2]/dx0 = 0
		//	d[sum( x_i - info_i )^2]/dv = 0
		//	d[sum( x_i - info_i )^2]/da = 0,
		// которая, в свою очередь, приводится к системе
		//	x0*n + v*[t1 + ... + tn] + a[(t1^2)/2 + ... + (tn^2)/2] = info_1 + ... + info_n
		//	x0*[t1 + ... + tn] + v*[t1^2 + ... + tn^2] + a[(t1^3)/2 + ... + (tn^3)/2] = info_1*t1 + ... + info_n*tn
		//	x0*[t1^2 + ... + tn^2] + v*[t1^3 + ... + tn^3] + a[(t1^4)/2 + ... + (tn^4)/2] = info_1*(t1^2) + ... + info_n*(tn^2)
		// или
		// x0*c1 + v0*c2 + a*c3 = c4
		// x0*c5 + v0*c6 + a*c7 = c8
		// x0*c9 + v0*c10 + a*c11 = c12
		// Решение, которой выглядит следующим образом в общем виде
		// x0 = (c4*c6*c11 + c8*c10*c3 + c2*c7*c12 - c12*c6*c3 - c8*c2*c11 - c10*c7*c4) / 
		// 	    (c1*c6*c11 + c5*c10*c3 + c2*c7*c9 - c9*c6*c3 - c5*c2*c11 - c10*c7*c1);     
		//  a = (c1*c8*c11 + c5*c12*c3 + c4*c7*c9 - c9*c8*c3 - c5*c4*c11 - c12*c7*c1) / 
		// 	    (c1*c6*c11 + c5*c10*c3 + c2*c7*c9 - c9*c6*c3 - c5*c2*c11 - c10*c7*c1);
		//  v = (c1*c6*c12 + c5*c10*c4 + c2*c8*c9 - c9*c6*c4 - c5*c2*c12 - c10*c8*c1) / 
		//	    (c1*c6*c11 + c5*c10*c3 + c2*c7*c9 - c9*c6*c3 - c5*c2*c11 - c10*c7*c1);

		double c1x = 0, c2x = 0, c3x = 0, c4x = 0, c5x = 0, c6x = 0, 
			   c7x = 0, c8x = 0, c9x = 0, c10x = 0, c11x = 0, c12x = 0,
			   c1y = 0, c2y = 0, c3y = 0, c4y = 0, c5y = 0, c6y = 0, 
			   c7y = 0, c8y = 0, c9y = 0, c10y = 0, c11y = 0, c12y = 0			   ;

		std::deque<trackInfo_t>::iterator it = info.begin();
		std::deque<trackInfo_t>::iterator itEnd = info.end();
		while(it != itEnd)
		{
			// Время измеряем дробно в секундах
			ITV8::timestamp_t ti = it->first - info.back().first;
			ITV8::PointF currentPos = CalcRectCenter(it->second);
			++c1x; ++c1y;
			c2x += ti; c2y += ti;
			c3x += ti*ti / 2.0; c3y += ti*ti / 2.0;
			c4x += currentPos.x; c4y += currentPos.y;
			c5x += ti; c5y += ti;
			c6x += ti*ti; c6y += ti*ti;
			c7x += ti*ti*ti / 2.0; c7y += ti*ti*ti / 2.0;
			c8x += currentPos.x * ti; c8y += currentPos.y * ti;
			c9x += ti*ti; c9y += ti*ti;
			c10x += ti*ti*ti; c10y += ti*ti*ti;
			c11x += ti*ti*ti*ti / 2.0; c11y += ti*ti*ti*ti / 2.0;
			c12x += currentPos.x * ti * ti; c12y += currentPos.y * ti * ti;
			++it;
		}

		x0 = (c4x*c6x*c11x + c8x*c10x*c3x + c2x*c7x*c12x - c12x*c6x*c3x - c8x*c2x*c11x - c10x*c7x*c4x) / 
			 (c1x*c6x*c11x + c5x*c10x*c3x + c2x*c7x*c9x - c9x*c6x*c3x - c5x*c2x*c11x - c10x*c7x*c1x);     
		vx = (c1x*c8x*c11x + c5x*c12x*c3x + c4x*c7x*c9x - c9x*c8x*c3x - c5x*c4x*c11x - c12x*c7x*c1x) / 
			(c1x*c6x*c11x + c5x*c10x*c3x + c2x*c7x*c9x - c9x*c6x*c3x - c5x*c2x*c11x - c10x*c7x*c1x);
		ax = (c1x*c6x*c12x + c5x*c10x*c4x + c2x*c8x*c9x - c9x*c6x*c4x - c5x*c2x*c12x - c10x*c8x*c1x) / 
			(c1x*c6x*c11x + c5x*c10x*c3x + c2x*c7x*c9x - c9x*c6x*c3x - c5x*c2x*c11x - c10x*c7x*c1x);

		y0 = (c4y*c6y*c11y + c8y*c10y*c3y + c2y*c7y*c12y - c12y*c6y*c3y - c8y*c2y*c11y - c10y*c7y*c4y) / 
			 (c1y*c6y*c11y + c5y*c10y*c3y + c2y*c7y*c9y - c9y*c6y*c3y - c5y*c2y*c11y - c10y*c7y*c1y);     
		vy = (c1y*c8y*c11y + c5y*c12y*c3y + c4y*c7y*c9y - c9y*c8y*c3y - c5y*c4y*c11y - c12y*c7y*c1y) / 
			(c1y*c6y*c11y + c5y*c10y*c3y + c2y*c7y*c9y - c9y*c6y*c3y - c5y*c2y*c11y - c10y*c7y*c1y);
		ay = (c1y*c6y*c12y + c5y*c10y*c4y + c2y*c8y*c9y - c9y*c6y*c4y - c5y*c2y*c12y - c10y*c8y*c1y) / 
			(c1y*c6y*c11y + c5y*c10y*c3y + c2y*c7y*c9y - c9y*c6y*c3y - c5y*c2y*c11y - c10y*c7y*c1y);
	}
}

//-------------- Вспомогательные функции распределения объектов по траекториям-------------
///////////////////////////////////////////////////////////////////////////////////////////
void CFaceTracker::DeleteOldTracks(const ITV8::timestamp_t& frameTime, std::set<ITV8::uint64_t>& trajectsSet,
								   const std::map<ITV8::uint64_t, ITV8::double_t>& borderDistances)
{
	std::set<ITV8::uint64_t>::iterator itTrajects = trajectsSet.begin();
	std::map<ITV8::uint64_t, std::deque<trackInfo_t> >::iterator itAllTracks = m_allTracks.begin();

	while (itTrajects != trajectsSet.end() && itAllTracks != m_allTracks.end())
	{
		itAllTracks = m_allTracks.find(*itTrajects);

		if (itAllTracks != m_allTracks.end())
		{		
			// Удаление по таймауту
			if ((frameTime - itAllTracks->second.front().first) > m_timeoutTime)
			{
				m_disappTracks[itAllTracks->first].push_front(trackInfo_t(frameTime, itAllTracks->second.front().second));
				itAllTracks = m_allTracks.erase(itAllTracks);
			}
			else
			{
				std::map<ITV8::uint64_t, ITV8::double_t>::const_iterator itBordDist = borderDistances.find(*itTrajects);
				// Удаление по близости к границе
				if (itBordDist != borderDistances.end())
				{
					ITV8::double_t rectWidth = itAllTracks->second.front().second.width;
					ITV8::double_t rectHeight = itAllTracks->second.front().second.height;
					ITV8::double_t avgSize = sqrt(rectWidth*rectWidth + rectHeight*rectHeight);

					// если трек близко к границе
					if (itBordDist->second < m_coeffNearness*avgSize)
					{
						m_disappTracks[itAllTracks->first].push_front(trackInfo_t(frameTime, itAllTracks->second.front().second));
						itAllTracks = m_allTracks.erase(itAllTracks);
					}
				}
			}

		}
		
		itTrajects = trajectsSet.erase(itTrajects);
	}
}

///////////////////////////////////////////////////////////////////////////////////////////
void CFaceTracker::CreateNewTracks(const ITV8::timestamp_t& frameTime, std::set<rectangleIndex_t>& rectsSet,
								  const std::vector<ITV8::RectangleF>& rects)
{
	auto itRects = rectsSet.begin();
	while (itRects != rectsSet.end())
	{
		m_allTracks[++m_lastID].push_front(trackInfo_t(frameTime, rects[*itRects]));
		m_appTracks[m_lastID].push_front(trackInfo_t(frameTime, rects[*itRects]));
		itRects = rectsSet.erase(itRects);
	}
}

///////////////////////////////////////////////////////////////////////////////////////////
void CFaceTracker::FillTrajectsMap(std::multimap<ITV8::double_t, std::pair<ITV8::uint64_t, rectangleIndex_t> >& distancesMap,
                                   rectangleIndexesSet_t& rectsSet, std::set<ITV8::uint64_t>& trajectsSet,
								   const std::vector<ITV8::RectangleF>& rects, const ITV8::timestamp_t& frameTime,
								   std::map<ITV8::uint64_t, std::deque<trackInfo_t> >& allTracks)
{
	// Алгоримт распредления объектов по траекториям следующий.
	// Обходим мапу по возрастанию расстояния и отбираем наилучшие соотвествия объекта.
	// Для найденных соответсвий выкидываем данную траекторию из набора траекторий,
	// а также выкидываем данный прямогульник из набора прямоугольников. Если для текущего
	// расстояния либо траектории, либо прямоугольника не содержится в соответствующих
	// множествах, то пропускаем этот элемент. Как только достигли порогового значения
	// расстояние - выходим из функции.

	auto itDistances = distancesMap.begin();	

	for (;itDistances != distancesMap.end() && itDistances->first <= m_distNewTrack && 
		 !trajectsSet.empty() && !rectsSet.empty(); ++itDistances)
	{
		ITV8::uint64_t currentTraject = itDistances->second.first;
        rectangleIndex_t currentRect = itDistances->second.second;
        
        // Итератор для отслеживаения "выкинутых" траекторий
		auto itTrajects = trajectsSet.find(currentTraject);
        // Итератор для отслеживаения "выкинутых" прямоугольников
		auto itRects = rectsSet.find(currentRect);

		// Если присутствуют "свободные" прямоугльник и траектория, то приписываем этот прямоугольник данной траектории
		if (itTrajects != trajectsSet.end() && itRects != rectsSet.end())
		{
			allTracks[currentTraject].push_front(trackInfo_t(frameTime, rects[currentRect]));
			rectsSet.erase(itRects);
			trajectsSet.erase(itTrajects);
		}
	}
}

///////////////////////////////////////////////////////////////////////////////////////////
void CFaceTracker::OnFrame(const ITV8::timestamp_t& frameTime, const std::vector<ITV8::RectangleF>& rects_)
{
    // очистим мапы, хранящие появившиеся/исчезнувшие треки
    m_appTracks.clear();
    m_disappTracks.clear();
    rectanglesList_t rects(rects_);

	// Выкинем "схожие" прямоугольники
	RemoveCloseRects(rects);


	// mapa для хранения расстояний каждого прямоугольника до каждой существующей траектории
	// ключом является расстояние до каждой из существующих к текущему моменту траекторий
	// элементами map'ы является пара (номер траектории, номер прямоугольника)
    distancesMap_t distances;

	// множество, хранящее номера прямоугольников
    rectangleIndexesSet_t currentRectsNums;

	// множество, хранящее номера текущих траекторий
	std::set<ITV8::uint64_t> currentTrajectsNums;

	// хранилище расстояний предсказанного положения до ближайшего края кадра
	std::map<ITV8::uint64_t, ITV8::double_t> borderDist;

	bool isEmptyMap = false;

	// Если хранилище треков пусто, то записываем этот прямоугольник с новым ID
	// и переходим к следующему переданному прямоугольнику
	if (m_allTracks.empty())
	{
		for (size_t i = 0; i < rects.size(); ++i)
		{
			m_allTracks[++m_lastID].push_front(trackInfo_t(frameTime, rects[i]));
            m_appTracks[m_lastID].push_front(trackInfo_t(frameTime, rects[i]));
			distances.insert(std::make_pair(0, std::make_pair(m_lastID, i)));
			currentTrajectsNums.insert(m_lastID);
		}
		isEmptyMap = true;
	}
	else
	{
		for (size_t i = 0; i < rects.size(); ++i)
		{

			// Заполним множество прямоугольников
			currentRectsNums.insert(i);

			const ITV8::RectangleF currentRect = rects[i];
			const ITV8::PointF centerCurrentRect = CalcRectCenter(currentRect);
		
			// Если хранилище треков не пусто
			std::map<ITV8::uint64_t, std::deque<trackInfo_t> >::iterator itAllTracks = m_allTracks.begin();
			for (;itAllTracks != m_allTracks.end(); ++itAllTracks)
			{
				// Заполним множество текущих траекторий, отследим чтобы это делалось один раз, а не на каждом шаге
				if (0 == i)
					currentTrajectsNums.insert(itAllTracks->first);	

				ITV8::PointF predPosition;
				if (1 == itAllTracks->second.size())
				{
					// если по данному треку имеем только одну рамку, то ее и считаем предсказанным положением
					predPosition = CalcRectCenter((itAllTracks->second)[0].second);
				}
				else
				{
					// Будем предсказывать положение объекта не дальше, чем время его жизни, т.к. дальнейшее
					// предсказание недостоверно.
					ITV8::timestamp_t predictTime;

					double dispersionX, dispersionY;
					getDispersion(itAllTracks->second, dispersionX, dispersionY);
					ITV8::double_t x0, y0, vx, vy, ax, ay;
					if (sqrt(fabs(dispersionX)) <= itAllTracks->second.front().second.width / 2.0 &&
						sqrt(fabs(dispersionY)) <= itAllTracks->second.front().second.height / 2.0)
					{
						ITV8::PointF centerStart = CalcRectCenter(itAllTracks->second.front().second);
						x0 = centerStart.x;
						y0 = centerStart.y;
						vx = 0.0;
						vy = 0.0;
						ax = 0.0;
						ay = 0.0;
					}
					else
					{
						leastSquareFunction(itAllTracks->second, x0, vx, ax, y0, vy, ay);
					}

					// Учли, что треки хранятся в очереди в обратном порядке - самый новый трек лижит ближе к началу
					// predictTime = t_конца + t_жизни = {t_жизни = t_конца - t_начала} = 2*t_конца - t_начала
					predictTime = std::min<ITV8::timestamp_t>(2*(itAllTracks->second).front().first, frameTime)
															  - (itAllTracks->second).back().first;
		
					ITV8::double_t xPredicted = x0 + vx * predictTime + ax * predictTime * predictTime / 2.0;
					ITV8::double_t yPredicted = y0 + vy * predictTime + ay * predictTime * predictTime / 2.0;
					predPosition = ITV8::PointF(xPredicted, yPredicted);
				}
				// Посчитаем расстояние от предрассчитанного положения точки и фактического
				ITV8::double_t dist = CalcDistBetweenPoints(predPosition, centerCurrentRect);
				// Теперь, присвоим нулевое расстояние, тем прямоугольникам, которые сильно пересекаются
				if (closenessEval((ITV8::RectangleF)currentRect, itAllTracks->second.front().second))
					dist = 0.0;
                distances.insert(std::make_pair(dist, std::make_pair(itAllTracks->first, i)));
				
				// Учитваем что мапа должна заполняться один раз
				if (borderDist.size() <= m_allTracks.size())
				{
					ITV8::double_t deltaX = std::min<ITV8::double_t>(predPosition.x, 1.0 - predPosition.x);
					ITV8::double_t deltaY = std::min<ITV8::double_t>(predPosition.y, 1.0 - predPosition.y);
					ITV8::double_t currentBordDist = sqrt(deltaX*deltaX + deltaY*deltaY);
					// Учтем в виде отрицательного расстояния, если объект оказался за пределами кадра
					if (deltaX*deltaY < 0) currentBordDist = -currentBordDist;

					borderDist[itAllTracks->first] = currentBordDist;
				}

			}
		}
	}

	if (!isEmptyMap)
	{
		// Припишем теперь трекам соответсвующие прямоугольники
		FillTrajectsMap(distances, currentRectsNums, currentTrajectsNums, rects, frameTime, m_allTracks);
		// Удалим треки, о которых не поступало информации дольше timeout-a
		DeleteOldTracks(frameTime, currentTrajectsNums, borderDist);
		// Для оставшихся прямоугольников создадим новые треки
		CreateNewTracks(frameTime, currentRectsNums, rects);
	}

}

void CFaceTracker::ForceFinishTracks(std::map<ITV8::uint64_t,ITV8::RectangleF> &disappearedTracks)
{
	 std::map<ITV8::uint64_t, std::deque<trackInfo_t> >::iterator itAllTracks = m_allTracks.begin();
	 std::map<ITV8::uint64_t, std::deque<trackInfo_t> >::iterator itAllTracksEnd = m_allTracks.end();
	 for (; itAllTracks != itAllTracksEnd; ++itAllTracks)
	 {
		 disappearedTracks[itAllTracks->first] = (itAllTracks->second).front().second;
	 }

     m_appTracks.clear();
     m_disappTracks.clear();
     m_allTracks.clear();
}

///////////////////////////////////////////////////////////////////////////////////////////
void CFaceTracker::GetCurrentTracks(std::map<ITV8::uint64_t, ITV8::RectangleF>& appearedTracks,
                                    std::map<ITV8::uint64_t, ITV8::RectangleF>& currentTracks,
                                    std::map<ITV8::uint64_t, ITV8::RectangleF>& disappearedTracks)
{
	std::map<ITV8::uint64_t, std::deque<trackInfo_t> >::iterator itMap = m_allTracks.begin();
	for (;itMap != m_allTracks.end(); ++itMap)
	{
		currentTracks[itMap->first] = (itMap->second).front().second;
	}

	std::map<ITV8::uint64_t, std::deque<trackInfo_t> >::iterator itAppMap = m_appTracks.begin();
    for (;itAppMap != m_appTracks.end(); ++itAppMap)
	{
		appearedTracks[itAppMap->first] = (itAppMap->second).front().second;
	}

    std::map<ITV8::uint64_t, std::deque<trackInfo_t> >::iterator itDisappMap = m_disappTracks.begin();
    for (;itDisappMap != m_disappTracks.end(); ++itDisappMap)
	{
		disappearedTracks[itDisappMap->first] = (itDisappMap->second).front().second;
	}
}
