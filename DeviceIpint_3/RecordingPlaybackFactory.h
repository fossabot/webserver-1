#ifndef DEVICEIPINT3_RECORDINGPLAYBACKFACTORY_H
#define DEVICEIPINT3_RECORDINGPLAYBACKFACTORY_H

#include <boost/thread.hpp>
#include <boost/bind.hpp>
#include <boost/asio/io_service.hpp>
#include <boost/shared_ptr.hpp>

#include <ItvDeviceSdk/include/IStorageDevice.h>
#include <ItvDeviceSdk/include/IRecordingPlayback.h>
#include "IpintDestroyer.h"
#include "StorageDataTypes.h"
#include "CIpInt30.h"

namespace IPINT30
{
typedef boost::shared_ptr<ITV8::GDRV::IRecordingPlayback> IRecordingPlaybackSP;

class RecordingPlaybackFactory
{
public:
    RecordingPlaybackFactory(ITV8::GDRV::IStorageDevice* storageDevice,
        NExecutors::PDynamicThreadPool dynExec,
        const std::string& recordingId,
        ITV8::Utility::tracksList_t& tracks,
        boost::weak_ptr<IPINT30::IIpintDevice> parent) :
        m_storageDevice(storageDevice),
        m_dynExec(dynExec),
        m_recordingId(recordingId),
        m_tracks(tracks),
        m_parent(parent)
    {}
    
    IRecordingPlaybackSP create(ITV8::MFF::IMultimediaFrameFactory* factory,
        ITV8::GDRV::IRecordingPlaybackHandler* handler)
    {
        auto lock_parent = m_parent.lock();
        if (!lock_parent)
            throw std::runtime_error("Playback can't be created while device is dead!");

        typedef boost::packaged_task<IRecordingPlaybackSP> task_t;
        task_t task(boost::bind(&RecordingPlaybackFactory::doCreate, 
            this, factory, handler));
        boost::unique_future<IRecordingPlaybackSP> future = task.get_future();
        if (!m_dynExec->Post(boost::bind(&task_t::operator(), &task)))
            throw std::runtime_error("Playback can't be created. Dynamic thread pool is owerloaded!");
        return future.get();
    }

private:
    IRecordingPlaybackSP doCreate(ITV8::MFF::IMultimediaFrameFactory* factory,
        ITV8::GDRV::IRecordingPlaybackHandler* handler)
    {
        if (!m_storageDevice)
        {
            return IRecordingPlaybackSP();
        }

        ITV8::Utility::TrackIdEnum enumerator(m_tracks);
        return IRecordingPlaybackSP(
            m_storageDevice->CreateRecordingPlayback(
                m_recordingId.c_str(), factory, enumerator, handler),
                ipint_destroyer<ITV8::GDRV::IRecordingPlayback>());
    }

private:
    ITV8::GDRV::IStorageDevice* const m_storageDevice;
    NExecutors::PDynamicThreadPool m_dynExec;
    const std::string m_recordingId;
    ITV8::Utility::tracksList_t m_tracks;
    boost::weak_ptr<IPINT30::IIpintDevice> m_parent;
};

}

#endif

