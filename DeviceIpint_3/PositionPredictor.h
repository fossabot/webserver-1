#include <boost/cstdint.hpp>
#include <boost/date_time.hpp>
#include <boost/thread/thread.hpp>

#include <Logging/log2.h>

#ifdef BOOST_NO_CXX11_CONSTEXPR
const
#else
constexpr
#endif
static boost::chrono::seconds minimalTrackDurationForPredictionInFullCamDelay{ 2 };
class CPositionPredictor
{
private:
	struct SObjectInfo
	{
		// Текущее положение объекта.
		double position;

		// Время "камеры", в которое эта позиция была зафиксирована
		boost::posix_time::ptime camTime;

		// Время "компьютера", в которое эта позиция была получена от камеры.
		// Данное поле добавлено для отсеивания предсказания для треков "с устаревшей информацией".
		boost::posix_time::ptime localTime;

		SObjectInfo(double const& position_, 
					boost::posix_time::ptime const& camTime_,
					boost::posix_time::ptime const& localTime_)
		:	position(position_)
		,	camTime(camTime_)
		,	localTime(localTime_)
		{}
	};
	
	// Хранилище x-координаты треков
	std::map<boost::uint32_t, std::vector<SObjectInfo> > m_TracksContainerX;

	// Хранилище y-координаты треков
	std::map<boost::uint32_t, std::vector<SObjectInfo> > m_TracksContainerY;

	// Задержка камеры
	boost::posix_time::time_duration m_CamDelay;

	// Функция решения задачи МНК для функционала x(t) = x0 + v*t + a * t * t / 2
	void leastSquareFunction(std::vector<SObjectInfo> &info, double &x0, double &v, double &a);

    DECLARE_LOGGER_HOLDER;
public:
	// В конструкторе сообщаем о том, какова задержка камеры (в миллисекундах)
	CPositionPredictor(DECLARE_LOGGER_ARG, const boost::posix_time::time_duration &camDelay);
	
	// Если задержка не передана, то считаем ее нулевой
	CPositionPredictor(DECLARE_LOGGER_ARG);
	~CPositionPredictor();

	// Функция добавления позиции трека в текущее хранилище
	// id - идентификатор трека
	// position - координата нижней центральной точки объекта
	// time - соответсвующее переданной позиции время
	void AddObjectPosition(const boost::uint32_t id, 
						   const std::pair<double, double> &position, 
						   const boost::posix_time::ptime &time);

	// Функция удаления трека с заданным id из хранилища
	// id - идентификатор трека
	void EraseObject(const boost::uint32_t id);

	// Функция осуществления предсказния
	// id - идентификатор трека, для которого будет осуществляться предсказние
	// predictedPosition - предсказанная позиция
	// Возврщаемое значение - успех или неудача
	enum EPrediction {eSuccess, eFail};
	EPrediction DoPrediction(const boost::uint32_t id, 
							 std::pair<double, double> &predictedPosition);
};
