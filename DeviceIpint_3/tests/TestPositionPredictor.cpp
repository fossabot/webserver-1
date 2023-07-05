#include "../PositionPredictor.h"

#include <boost/thread/thread.hpp>
#include <boost/test/unit_test.hpp>

BOOST_AUTO_TEST_SUITE(TestPositionPredictor)
////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Предсказание по пустому хранилищу треков. Ожидаемый результат eFail.
BOOST_AUTO_TEST_CASE(EmptyPredictionTest)
{
    CPositionPredictor object(0, boost::posix_time::millisec(500));
    std::pair<double, double> prediction;
    BOOST_CHECK(object.DoPrediction(1, prediction) == CPositionPredictor::eFail);
}

// Предсказание по id, которого не содержится в хранилище. Ожидаемый результат eFail.
BOOST_AUTO_TEST_CASE(InvalidIdPredictionTest)
{
    CPositionPredictor object(0, boost::posix_time::millisec(500));
    std::pair<double, double> prediction;
    // Запишем фиктивный трек
    for (int i = 0; i < 10; ++i)
    {
        object.AddObjectPosition(1, std::make_pair(i * 0.05, i * 0.09),
        boost::posix_time::ptime(boost::date_time::min_date_time) + boost::posix_time::millisec(40 * i));
    }
    BOOST_CHECK(object.DoPrediction(2, prediction) == CPositionPredictor::eFail);
}

// Предсказание по id, которого УЖЕ не содержится в хранилище, т.е. удалили из него. Ожидаемый результат eFail.
BOOST_AUTO_TEST_CASE(ZombieIdPredictionTest)
{
    CPositionPredictor object(0, boost::posix_time::millisec(500));
    std::pair<double, double> prediction;
    // Запишем фиктивный трек
    boost::posix_time::ptime currentTime = boost::posix_time::second_clock::local_time();
    for (int i = 0; i < 10; ++i)
    {
        object.AddObjectPosition(1, std::make_pair(i * 0.05, i * 0.09),
                                 currentTime + boost::posix_time::millisec(40 * i));
        object.AddObjectPosition(0, std::make_pair(i * 0.2 / 3.0, i * 0.05),
                                 currentTime + boost::posix_time::millisec(100 * i));
    }
    BOOST_CHECK(object.DoPrediction(1, prediction) == CPositionPredictor::eSuccess);
    object.EraseObject(1);
    BOOST_CHECK(object.DoPrediction(1, prediction) == CPositionPredictor::eFail);
}

// Предсказание по треку, информация о котором уже давно не обновлялась. Ожидаемый результат eFail.
BOOST_AUTO_TEST_CASE(OldInfoIdPredictionTest)
{
    CPositionPredictor object(0, boost::posix_time::millisec(500));
    std::pair<double, double> prediction;
    // Запишем фиктивный трек
    for (int i = 0; i < 10; ++i)
    {
        object.AddObjectPosition(1, std::make_pair(i * 0.05, i * 0.09),
               boost::posix_time::ptime(boost::date_time::min_date_time) + boost::posix_time::millisec(40 * i));
    }
    boost::this_thread::sleep_for(boost::chrono::nanoseconds(4000000000L));
    BOOST_CHECK(object.DoPrediction(1, prediction) == CPositionPredictor::eFail);
}

// Предсказание по статичному треку. Ожидаемый результат eSuccess.
BOOST_AUTO_TEST_CASE(StaticIdPredictionTest)
{
    CPositionPredictor object(0, boost::posix_time::millisec(500));
    std::pair<double, double> prediction;
    // Запишем фиктивный трек
    for (int i = 0; i < 10; ++i)
    {
        object.AddObjectPosition(1, std::make_pair(0.05, 0.09),
               boost::posix_time::ptime(boost::date_time::min_date_time) + boost::posix_time::millisec(40 * i));
    }
    BOOST_CHECK(object.DoPrediction(1, prediction) == CPositionPredictor::eSuccess);
    BOOST_CHECK(fabs(prediction.first - 0.05) < 0.00001);
    BOOST_CHECK(fabs(prediction.second - 0.09) < 0.00001);
}

// Предсказание по статичному треку из 2х положений. Ожидаемый результат eSuccess.
BOOST_AUTO_TEST_CASE(StaticIdTwoPositionPredictionTest)
{
    CPositionPredictor object(0, boost::posix_time::millisec(500));
    std::pair<double, double> prediction;
    // Запишем фиктивный трек
    for (int i = 0; i < 2; ++i)
    {
        object.AddObjectPosition(1, std::make_pair(0.05, 0.09),
               boost::posix_time::ptime(boost::date_time::min_date_time) + boost::posix_time::millisec(40 * i));
    }
    BOOST_CHECK(object.DoPrediction(1, prediction) == CPositionPredictor::eSuccess);
    BOOST_CHECK(fabs(prediction.first - 0.05) < 0.00001);
    BOOST_CHECK(fabs(prediction.second - 0.09) < 0.00001);
}

// Предсказание по треку с нулевой задержкой камеры. Ожидаемый результат eSuccess и прогноз является последним положением объекта.
BOOST_AUTO_TEST_CASE(NoDelayPredictionTest)
{
    CPositionPredictor object(0, boost::posix_time::millisec(0));
    std::pair<double, double> prediction;
    // Запишем фиктивный трек
    for (int i = 0; i < 10; ++i)
    {
        object.AddObjectPosition(1, std::make_pair(i * 0.05, i * 0.09),
               boost::posix_time::ptime(boost::date_time::min_date_time) + boost::posix_time::millisec(40 * i));
    }
    BOOST_CHECK(object.DoPrediction(1, prediction) == CPositionPredictor::eSuccess);
    BOOST_CHECK(fabs(prediction.first - 9*0.05) < 0.00001);
    BOOST_CHECK(fabs(prediction.second - 9*0.09) < 0.00001);
}

// Предсказание по треку с нулевым смещение по Y. Ожидаемый результат eSuccess и cоответсвуюещее положение предсказания.
BOOST_AUTO_TEST_CASE(XShiftPredictionTest)
{
    CPositionPredictor object(0, boost::posix_time::millisec(40));
    std::pair<double, double> prediction;
    // Запишем фиктивный трек
    const auto numSamples = 1 + boost::chrono::milliseconds(minimalTrackDurationForPredictionInFullCamDelay).count() / 40;
    for (int i = 0; i < numSamples; ++i)
    {
        object.AddObjectPosition(1, std::make_pair(i * 0.05, 0.09),
               boost::posix_time::ptime(boost::date_time::min_date_time) + boost::posix_time::millisec(40 * i));
    }
    BOOST_CHECK(object.DoPrediction(1, prediction) == CPositionPredictor::eSuccess);
    BOOST_CHECK(fabs(prediction.first - numSamples*0.05) < 0.000001 * numSamples);
    BOOST_CHECK(fabs(prediction.second - 0.09) < 0.00001);
}

// Предсказание по треку с нулевым смещение по X. Ожидаемый результат eSuccess и cоответсвуюещее положение предсказания.
BOOST_AUTO_TEST_CASE(YShiftPredictionTest)
{
    CPositionPredictor object(0, boost::posix_time::millisec(40));
    std::pair<double, double> prediction;
    // Запишем фиктивный трек
    const auto numSamples = 1 + boost::chrono::milliseconds(minimalTrackDurationForPredictionInFullCamDelay).count() / 40;
    for (int i = 0; i < numSamples; ++i)
    {
        object.AddObjectPosition(1, std::make_pair(0.05, i*0.09),
               boost::posix_time::ptime(boost::date_time::min_date_time) + boost::posix_time::millisec(40 * i));
    }
    BOOST_CHECK(object.DoPrediction(1, prediction) == CPositionPredictor::eSuccess);
    BOOST_CHECK(fabs(prediction.first - 0.05) < 0.00001);
    BOOST_CHECK(fabs(prediction.second - numSamples*0.09) < 0.000001 * numSamples);
}

// Предсказание по треку, у которого часть положений, передается с одним и тем же временем.
// Ожидаемый результат eSuccess и cоответсвуюещее положение предсказания.
BOOST_AUTO_TEST_CASE(SameTimePredictionTest)
{
    CPositionPredictor object(0, boost::posix_time::millisec(40));
    std::pair<double, double> prediction;
    // Запишем фиктивный трек
    const auto numSamples = 1 + boost::chrono::milliseconds(minimalTrackDurationForPredictionInFullCamDelay).count() / 40;
    for (int i = 0; i < numSamples; ++i)
    {
        object.AddObjectPosition(1, std::make_pair(i*0.05, i*0.09),
               boost::posix_time::ptime(boost::date_time::min_date_time) + boost::posix_time::millisec(40 * i));
    }
    for (int i = 4; i >= 0; --i)
    {
        object.AddObjectPosition(1, std::make_pair(i*0.05, i*0.09),
               boost::posix_time::ptime(boost::date_time::min_date_time) + boost::posix_time::millisec(40 * (numSamples - 1)));
    }
    BOOST_CHECK(object.DoPrediction(1, prediction) == CPositionPredictor::eSuccess);
    BOOST_CHECK(fabs(prediction.first - numSamples*0.05) < 0.000001 * numSamples);
    BOOST_CHECK(fabs(prediction.second - numSamples*0.09) < 0.000001 * numSamples);
}

// Предсказание по треку, который движется по закону
//		x = 0.2 + 0.05*t + 0.01 * t * t / 2
//		y = 0.5 - 0.1 * t + 0.05 * t * t / 2
// Ожидаемый результат через 5 сек. eSuccess и cоответсвуюещее положение предсказания.
BOOST_AUTO_TEST_CASE(ModelPredictionTest)
{
    CPositionPredictor object(0, boost::posix_time::millisec(500));
    std::pair<double, double> prediction;
    // Запишем фиктивный трек
    const auto numSamples = std::max<int>(10, 1 +
        static_cast<int>(boost::chrono::milliseconds(minimalTrackDurationForPredictionInFullCamDelay).count() / 500));
    for (int i = 0; i < numSamples; ++i)
    {
        boost::posix_time::ptime currentTime = boost::posix_time::ptime(boost::date_time::min_date_time) + boost::posix_time::millisec(500 * i);
        double dt = 500.0 * i / 1000.0;
        double x =  0.2 + 0.05 * dt + 0.01 * dt * dt / 2;
        double y =  0.5 - 0.1 * dt + 0.05 * dt * dt / 2;
        object.AddObjectPosition(1, std::make_pair(x, y), currentTime);
    }
    double tNext = 500.0 * numSamples / 1000.0;
    double xNext = 0.2 + 0.05 * tNext + 0.01 * tNext * tNext / 2;
    double yNext =  0.5 - 0.1 * tNext + 0.05 * tNext * tNext / 2;
    BOOST_CHECK(object.DoPrediction(1, prediction) == CPositionPredictor::eSuccess);
    BOOST_CHECK(fabs(prediction.first - xNext) < 0.000001 * numSamples);
    BOOST_CHECK(fabs(prediction.second - yNext) < 0.000001 * numSamples);
}

BOOST_AUTO_TEST_SUITE_END() // TestPositionPredictor
