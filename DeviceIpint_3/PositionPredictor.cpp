#include "PositionPredictor.h"

//////////////////////////////////////////////////////////////////////////////////////////
CPositionPredictor::CPositionPredictor(DECLARE_LOGGER_ARG)
:	m_CamDelay(boost::posix_time::millisec(0))
{
    INIT_LOGGER_HOLDER;
}

//////////////////////////////////////////////////////////////////////////////////////////
CPositionPredictor::CPositionPredictor(DECLARE_LOGGER_ARG, const boost::posix_time::time_duration &camDelay)
:	m_CamDelay(camDelay)
{
    INIT_LOGGER_HOLDER;
}

//////////////////////////////////////////////////////////////////////////////////////////
CPositionPredictor::~CPositionPredictor()
{
	m_TracksContainerX.clear();
	m_TracksContainerY.clear();
}

//////////////////////////////////////////////////////////////////////////////////////////
void CPositionPredictor::AddObjectPosition(const boost::uint32_t id,
										   const std::pair<double, double> &position,
										   const boost::posix_time::ptime &time)
{
	boost::posix_time::ptime localTime = boost::posix_time::second_clock::local_time();
	// Проверим корректность переданных данных
	assert(position.first >= 0 && position.second >= 0);
	// проследим, чтобы не записать положения с одинаковым временем для одного id
	if (!m_TracksContainerX[id].empty())
	{
		//assert(time != m_TracksContainerX[id].back().camTime);
		if (m_TracksContainerX[id].back().camTime == time)
        {
            //_log_ << "Object " << id << " has same last time.";
			return;
        }
	}

	m_TracksContainerX[id].emplace_back(position.first, time, localTime);
	m_TracksContainerY[id].emplace_back(position.second, time, localTime);
}

//////////////////////////////////////////////////////////////////////////////////////////
void CPositionPredictor::EraseObject(const boost::uint32_t id)
{
	m_TracksContainerX.erase(id);
	m_TracksContainerY.erase(id);
}

//////////////////////////////////////////////////////////////////////////////////////////
void CPositionPredictor::leastSquareFunction(std::vector<SObjectInfo> &info, double &x0, double &v, double &a)
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

	double c1 = 0, c2 = 0, c3 = 0, c4 = 0, c5 = 0, c6 = 0,
		   c7 = 0, c8 = 0, c9 = 0, c10 = 0, c11 = 0, c12 = 0;
	for (int i = 0; i < int(info.size()); ++i)
	{
		// Время измеряем дробно в секундах
		double ti = 1.0*(info[i].camTime - info[0].camTime).total_milliseconds() / 1000.0;
		++c1;
		c2 += ti;
		c3 += ti*ti / 2.0;
		c4 += info[i].position;
		c5 += ti;
		c6 += ti*ti;
		c7 += ti*ti*ti / 2.0;
		c8 += info[i].position * ti;
		c9 += ti*ti;
		c10 += ti*ti*ti;
		c11 += ti*ti*ti*ti / 2.0;
		c12 += info[i].position * ti * ti;
	}

	x0 = (c4*c6*c11 + c8*c10*c3 + c2*c7*c12 - c12*c6*c3 - c8*c2*c11 - c10*c7*c4) /
		 (c1*c6*c11 + c5*c10*c3 + c2*c7*c9 - c9*c6*c3 - c5*c2*c11 - c10*c7*c1);
	v = (c1*c8*c11 + c5*c12*c3 + c4*c7*c9 - c9*c8*c3 - c5*c4*c11 - c12*c7*c1) /
		(c1*c6*c11 + c5*c10*c3 + c2*c7*c9 - c9*c6*c3 - c5*c2*c11 - c10*c7*c1);
	a = (c1*c6*c12 + c5*c10*c4 + c2*c8*c9 - c9*c6*c4 - c5*c2*c12 - c10*c8*c1) /
		(c1*c6*c11 + c5*c10*c3 + c2*c7*c9 - c9*c6*c3 - c5*c2*c11 - c10*c7*c1);
}

//////////////////////////////////////////////////////////////////////////////////////////
CPositionPredictor::EPrediction CPositionPredictor::DoPrediction(const boost::uint32_t id,
											std::pair<double,double> &predictedPosition)
{
	// Замерим время вызова функции
	boost::posix_time::ptime localTime = boost::posix_time::second_clock::local_time();

	// Будем аппроксимировать траекторию объекта двумя функциями:
	//			X(t) = X0 + V_{0x} * t + A_{x} * t * t / 2
	//			Y(t) = Y0 + V_{0y} * t + A_{y} * t * t / 2
	double x0, vx, ax, y0, vy, ay;

    if (m_TracksContainerX.end() == m_TracksContainerX.find(id) ||
        m_TracksContainerY.end() == m_TracksContainerY.find(id))
    {
        return eFail;
    }

	std::vector<SObjectInfo>& trackX = m_TracksContainerX[id];
	std::vector<SObjectInfo>& trackY = m_TracksContainerY[id];

	// Проверим, можем ли мы произвести предсказание по этому треку. Для этого необходимо
	//		1. Чтобы контейнеры trackX и trackY были непустыми и содержали более одной позиции.
	//		2. Чтобы за последние 3 секунды приходила новая информация с вызовом функции AddObjectPosition
	// Учтем, что у trackX и trackY поэлементно одинаковый localTime и camTime.
	if (trackX.empty() || trackY.empty() || (localTime - trackX.back().localTime).total_seconds() > 3)
    {
        //_log_ << "Object " << id << " has no enought information.";
		return eFail;
    }

	// Определим с какого момента начнем учитывать траекторию объекта, т.е. найдем элемент, отстоящий от последнего
	// не более, чем на 10 сек. Учли, что у trackX и trackY поэлементно одинаковый localTime и camTime.
	int containerSize = (int)trackX.size();
	int startIndex = containerSize - 1;
	for (int i = containerSize - 1; i >= 0; --i)
	{
		if ((trackX.back().camTime - trackX[i].camTime).total_seconds() > 10)
			break;
		else
			startIndex = i;
	}

    //_log_ << "Object " << id << " containerSize: " << containerSize << "; startIndex: " << startIndex;
	// Определим теперь параметры vx, ax, vy, ay;
	// Если контейнер состоит всего из 2х элементов, то оцениваем траекторию линейно
	//			X(t) = V_{0x} * t + 0 * t * t / 2
	//			Y(t) = V_{0y} * t + 0 * t * t / 2
    if (1 == containerSize)
    {
        x0 = trackX.front().position;
        vx = 0;
        ax = 0;
        y0 = trackY.front().position;
        vy = 0;
        ay = 0;
    }
	else if (2 == containerSize)
	{
		double dt = (trackX.back().camTime - trackX.front().camTime).total_milliseconds() / 1000.0;
		x0 = trackX.front().position;
		vx = (trackX.back().position - trackX.front().position) / dt;
		ax = 0;
		y0 = trackY.front().position;
		vy = (trackY.back().position - trackY.front().position) / dt;
		ay = 0;
	}
	else
	{
		std::vector<SObjectInfo>::iterator firstX = trackX.begin() + startIndex;
		std::vector<SObjectInfo> infoX(firstX, trackX.end());
		leastSquareFunction(infoX, x0, vx, ax);

		std::vector<SObjectInfo>::iterator firstY = trackY.begin() + startIndex;
		std::vector<SObjectInfo> infoY(firstY, trackY.end());
		leastSquareFunction(infoY, y0, vy, ay);
	}

	// Предскажем позицию трека
    // Для треков, которые живут меньше 2 секунд будем пропорционально уменьшать время предсказания
    boost::chrono::milliseconds trackDuration { (trackX.back().camTime - trackX[startIndex].camTime).total_milliseconds() };
    boost::chrono::duration<double> dt = trackDuration, dDelay { m_CamDelay.total_milliseconds() / 1000.0 };
    if ( trackDuration <= minimalTrackDurationForPredictionInFullCamDelay )
        dDelay *= dt / minimalTrackDurationForPredictionInFullCamDelay;
    double tn = (dt + dDelay).count();
	predictedPosition.first = x0 + vx * tn + ax * tn * tn / 2.0;
	predictedPosition.second = y0 + vy * tn + ay * tn * tn / 2.0;
	return eSuccess;
}

