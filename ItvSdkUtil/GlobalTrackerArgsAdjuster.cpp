#include "GlobalTrackerArgsAdjuster.h"

#include <ItvFramework/TimeConverter.h>
#include <ItvDetectorSdk/include/ItvDetectorsSdk2.h>

#include <axxonsoft/bl/metadata/MetadataService.pb.h>

#include <boost/date_time/posix_time/time_formatters.hpp>

#include "../MMClient/DetectorEventFactory.h"

axxonsoft::bl::metadata::ObjectState ToBlObjectState(ITV8::Analytics::ETargetState state)
{
    switch (state)
    {
        case ITV8::Analytics::Appeared:
            return axxonsoft::bl::metadata::OBJECT_STATE_APPEARED;
        case ITV8::Analytics::Normal:
            return axxonsoft::bl::metadata::OBJECT_STATE_NORMAL;
        case ITV8::Analytics::Disappeared:
            return axxonsoft::bl::metadata::OBJECT_STATE_DISAPPEARED;
        default:
            return axxonsoft::bl::metadata::OBJECT_STATE_UNSPECIFIED;
    }
}

axxonsoft::bl::metadata::ObjectType ToBlObjectType(ITV8::Analytics::EObjectClass objectClass)
{
    switch (objectClass)
    {
        case ITV8::Analytics::EUnknownObject:
            return axxonsoft::bl::metadata::OBJECT_TYPE_UNSPECIFIED;
        case ITV8::Analytics::EHumanObject:
            return axxonsoft::bl::metadata::OBJECT_TYPE_HUMAN;
        case ITV8::Analytics::EGroupOfHumansObject:
            return axxonsoft::bl::metadata::OBJECT_TYPE_GROUP_OF_HUMANS;
        case ITV8::Analytics::EVehicleObject:
            return axxonsoft::bl::metadata::OBJECT_TYPE_VEHICLE;
        case ITV8::Analytics::EFaceObject:
            return axxonsoft::bl::metadata::OBJECT_TYPE_FACE;
        case ITV8::Analytics::EAnimalObject:
            return axxonsoft::bl::metadata::OBJECT_TYPE_ANIMAL;
        case ITV8::Analytics::ERobotDogObject:
            return axxonsoft::bl::metadata::OBJECT_TYPE_ROBOT_DOG;
        case ITV8::Analytics::EChildObject:
            return axxonsoft::bl::metadata::OBJECT_TYPE_CHILD;
        case ITV8::Analytics::ECatObject:
            return axxonsoft::bl::metadata::OBJECT_TYPE_CAT;
        default:
            return axxonsoft::bl::metadata::OBJECT_TYPE_UNSPECIFIED;
    }
}

ITV8::hresult_t GlobalTrackerArgsAdjuster::SetMultimediaBuffer(const std::string& name,
    boost::shared_ptr<ITV8::MFF::IMultimediaBuffer> multimediaBuffer)
{
    auto* globalTracks = ITV8::contract_cast<ITV8::Analytics::IGlobalTrackEnumerator>(multimediaBuffer.get());

    if (!globalTracks)
    {
        return BaseEventArgsAdjuster::SetMultimediaBuffer(name, multimediaBuffer);
    }

    axxonsoft::bl::metadata::GlobalTracklets globalTracklets;
    iterateEnumerator(globalTracks, [&](ITV8::Analytics::IGlobalTrack* globalTrack) mutable
    {
        auto* tracklet = globalTracklets.add_tracklets();
        tracklet->set_guid(globalTrack->GetGuid());
        tracklet->set_state(ToBlObjectState(globalTrack->GetState()));
        tracklet->set_type(ToBlObjectType(globalTrack->GetObjectClass()));

        iterateEnumerator(globalTrack->GetPositions(), [&](const ITV8::Analytics::IMapPoint* mapPoint)
        {
            auto* onMapPosition = tracklet->add_on_map_positions();
            onMapPosition->set_map_id(mapPoint->GetMapId());

            const auto position = mapPoint->GetPoint();
            auto* blPosition = onMapPosition->mutable_point();
            blPosition->set_x(position.x);
            blPosition->set_y(position.y);
            blPosition->set_z(position.z);

            return ITV8::ENotError;
        });

        iterateEnumerator(globalTrack->GetVelocities(), [&](const ITV8::Analytics::IMapPoint* mapPoint)
        {
            auto* velocity = tracklet->add_velocities();
            velocity->set_map_id(mapPoint->GetMapId());

            const auto v = mapPoint->GetPoint();
            auto* blVelocity = velocity->mutable_point();
            blVelocity->set_x(v.x);
            blVelocity->set_y(v.y);
            blVelocity->set_z(v.z);

            return ITV8::ENotError;
        });

        iterateEnumerator(globalTrack->GetOnCameraPositions(), [&](const ITV8::Analytics::ICameraFrameArea* onCameraPosition)
        {
            auto* blOnCameraPosition = tracklet->add_on_camera_positions();
            blOnCameraPosition->set_timestamp(
                boost::posix_time::to_iso_string(ITV8::PtimeFromTimestamp(onCameraPosition->GetTimestamp())));
            blOnCameraPosition->set_camera_id(onCameraPosition->GetCameraId());

            auto* blArea = blOnCameraPosition->mutable_area();
            const auto area = onCameraPosition->GetArea();
            blArea->set_x(area.left);
            blArea->set_y(area.top);
            blArea->set_w(area.width);
            blArea->set_h(area.height);

            return ITV8::ENotError;
        });

        return ITV8::ENotError;
    });

    event()->SetProtobufMessage(globalTracklets.SerializeAsString());

    return ITV8::ENotError;
}
