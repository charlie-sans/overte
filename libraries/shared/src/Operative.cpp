//
//  Operative.cpp
//  hifi
//
//  Created by Stephen Birarda on 7/1/13.
//  Copyright (c) 2013 HighFidelity, Inc. All rights reserved.
//

#include "AudioInjectionManager.h"

#include "NodeList.h"
#include "NodeTypes.h"
#include "PacketHeaders.h"
#include "SharedUtil.h"

#include "Operative.h"

const float BUG_VOXEL_SIZE = 0.0625f / 128;

glm::vec3 rotatePoint(glm::vec3 point, float angle) {
    //  First, create the quaternion based on this angle of rotation
    glm::quat q(glm::vec3(0, -angle, 0));
    
    //  Next, create a rotation matrix from that quaternion
    glm::mat4 rotation = glm::mat4_cast(q);
    
    //  Transform the original vectors by the rotation matrix to get the new vectors
    glm::vec4 quatPoint(point.x, point.y, point.z, 0);
    glm::vec4 newPoint = quatPoint * rotation;
    
    return glm::vec3(newPoint.x, newPoint.y, newPoint.z);
}

Operative::Operative() :
    _bugPosition(BUG_VOXEL_SIZE * 20.0, BUG_VOXEL_SIZE * 30.0, BUG_VOXEL_SIZE * 20.0),
    _bugDirection(0, 0, 1),
    _bugPathCenter(BUG_VOXEL_SIZE * 150.0, BUG_VOXEL_SIZE * 30.0, BUG_VOXEL_SIZE * 150.0),
    _bugPathRadius(BUG_VOXEL_SIZE * 40.0),
    _bugPathTheta(0.0f),
    _bugRotation(0.0f),
    _bugAngleDelta(0.2 * (M_PI / 180.0f)),
    _moveBugInLine(false),
    _packetsSent(0),
    _bytesSent(0)
{
}

class BugPart {
public:
    glm::vec3       partLocation;
    unsigned char   partColor[3];
    
    BugPart(const glm::vec3& location, unsigned char red, unsigned char green, unsigned char blue ) {
        partLocation = location;
        partColor[0] = red;
        partColor[1] = green;
        partColor[2] = blue;
    }
};

const BugPart BUG_PARTS[VOXELS_PER_BUG] = {
    
    // tail
    BugPart(glm::vec3( 0, 0, -3), 51, 51, 153) ,
    BugPart(glm::vec3( 0, 0, -2), 51, 51, 153) ,
    BugPart(glm::vec3( 0, 0, -1), 51, 51, 153) ,
    
    // body
    BugPart(glm::vec3( 0, 0,  0), 255, 200, 0) ,
    BugPart(glm::vec3( 0, 0,  1), 255, 200, 0) ,
    
    // head
    BugPart(glm::vec3( 0, 0,  2), 200, 0, 0) ,
    
    // eyes
    BugPart(glm::vec3( 1, 0,  3), 64, 64, 64) ,
    BugPart(glm::vec3(-1, 0,  3), 64, 64, 64) ,
    
    // wings
    BugPart(glm::vec3( 3, 1,  1), 0, 153, 0) ,
    BugPart(glm::vec3( 2, 1,  1), 0, 153, 0) ,
    BugPart(glm::vec3( 1, 0,  1), 0, 153, 0) ,
    BugPart(glm::vec3(-1, 0,  1), 0, 153, 0) ,
    BugPart(glm::vec3(-2, 1,  1), 0, 153, 0) ,
    BugPart(glm::vec3(-3, 1,  1), 0, 153, 0) ,
    
    
    BugPart(glm::vec3( 2, -1,  0), 153, 200, 0) ,
    BugPart(glm::vec3( 1, -1,  0), 153, 200, 0) ,
    BugPart(glm::vec3(-1, -1,  0), 153, 200, 0) ,
    BugPart(glm::vec3(-2, -1,  0), 153, 200, 0) ,
};

void Operative::removeOldBug() {
    VoxelDetail details[VOXELS_PER_BUG];
    unsigned char* bufferOut;
    int sizeOut;
    
    // Generate voxels for where bug used to be
    for (int i = 0; i < VOXELS_PER_BUG; i++) {
        details[i].s = BUG_VOXEL_SIZE;
        
        glm::vec3 partAt = BUG_PARTS[i].partLocation * BUG_VOXEL_SIZE * (_bugDirection.x < 0 ? -1.0f : 1.0f);
        glm::vec3 rotatedPartAt = rotatePoint(partAt, _bugRotation);
        glm::vec3 offsetPartAt = rotatedPartAt + _bugPosition;
        
        details[i].x = offsetPartAt.x;
        details[i].y = offsetPartAt.y;
        details[i].z = offsetPartAt.z;
        
        details[i].red   = BUG_PARTS[i].partColor[0];
        details[i].green = BUG_PARTS[i].partColor[1];
        details[i].blue  = BUG_PARTS[i].partColor[2];
    }
    
    // send the "erase message" first...
    PACKET_TYPE message = PACKET_TYPE_ERASE_VOXEL;
    if (createVoxelEditMessage(message, 0, VOXELS_PER_BUG, (VoxelDetail*)&details, bufferOut, sizeOut)){
        
        _packetsSent++;
        _bytesSent += sizeOut;
        
        NodeList::getInstance()->broadcastToNodes(bufferOut, sizeOut, &NODE_TYPE_VOXEL_SERVER, 1);
        delete[] bufferOut;
    }
}

void Operative::renderMovingBug() {
    unsigned char* bufferOut;
    int sizeOut;
    
    removeOldBug();
    
    // Move the bug...
    if (_moveBugInLine) {
        _bugPosition.x += (_bugDirection.x * BUG_VOXEL_SIZE);
        _bugPosition.y += (_bugDirection.y * BUG_VOXEL_SIZE);
        _bugPosition.z += (_bugDirection.z * BUG_VOXEL_SIZE);
        
        // Check boundaries
        if (_bugPosition.z > 1.0) {
            _bugDirection.z = -1;
        }
        if (_bugPosition.z < BUG_VOXEL_SIZE) {
            _bugDirection.z = 1;
        }
    } else {
        
        _bugPathTheta += _bugAngleDelta; // move slightly
        _bugRotation  -= _bugAngleDelta; // rotate slightly
        
        // If we loop past end of circle, just reset back into normal range
        if (_bugPathTheta > (360.0f * PI_OVER_180)) {
            _bugPathTheta = 0;
            _bugRotation  = 0;
        }
        
        float x = _bugPathCenter.x + _bugPathRadius * cos(_bugPathTheta);
        float z = _bugPathCenter.z + _bugPathRadius * sin(_bugPathTheta);
        float y = _bugPathCenter.y;
        
        _bugPosition = glm::vec3(x, y, z);
    }
    
    // would be nice to add some randomness here...
    
    // Generate voxels for where bug is going to
    for (int i = 0; i < VOXELS_PER_BUG; i++) {
        _bugDetails[i].s = BUG_VOXEL_SIZE;
        
        glm::vec3 partAt = BUG_PARTS[i].partLocation * BUG_VOXEL_SIZE * (_bugDirection.x < 0 ? -1.0f : 1.0f);
        glm::vec3 rotatedPartAt = rotatePoint(partAt, _bugRotation);
        glm::vec3 offsetPartAt = rotatedPartAt + _bugPosition;
        
        _bugDetails[i].x = offsetPartAt.x;
        _bugDetails[i].y = offsetPartAt.y;
        _bugDetails[i].z = offsetPartAt.z;
        
        _bugDetails[i].red   = BUG_PARTS[i].partColor[0];
        _bugDetails[i].green = BUG_PARTS[i].partColor[1];
        _bugDetails[i].blue  = BUG_PARTS[i].partColor[2];
    }
    
    // send the "create message" ...
    PACKET_TYPE message = PACKET_TYPE_SET_VOXEL_DESTRUCTIVE;
    if (createVoxelEditMessage(message, 0, VOXELS_PER_BUG, (VoxelDetail*)&_bugDetails, bufferOut, sizeOut)){
        
        _packetsSent++;
        _bytesSent += sizeOut;
        
        NodeList::getInstance()->broadcastToNodes(bufferOut, sizeOut, &NODE_TYPE_VOXEL_SERVER, 1);
        delete[] bufferOut;
    }
}

const int ACTUAL_FPS = 60;
const double OUR_FPS_IN_MILLISECONDS = 1000.0/ACTUAL_FPS; // determines FPS from our desired FPS
const int ANIMATE_VOXELS_INTERVAL_USECS = OUR_FPS_IN_MILLISECONDS * 1000.0; // converts from milliseconds to usecs

void Operative::run() {
    timeval lastSendTime = {};
    timeval lastDomainServerCheckIn = {};
    
    NodeList* nodeList = NodeList::getInstance();
    
    sockaddr nodePublicAddress;
    
    unsigned char* packetData = new unsigned char[MAX_PACKET_SIZE];
    ssize_t receivedBytes;
    
    // change the owner type on our NodeList
    NodeList::getInstance()->setOwnerType(NODE_TYPE_AGENT);
    const char NODE_TYPES_OF_INTEREST[] = {NODE_TYPE_VOXEL_SERVER, NODE_TYPE_AUDIO_MIXER};
    NodeList::getInstance()->setNodeTypesOfInterest(NODE_TYPES_OF_INTEREST, 2);
    NodeList::getInstance()->getNodeSocket()->setBlocking(false);
    
    while (!shouldStop) {
        gettimeofday(&lastSendTime, NULL);
        
        renderMovingBug();
        
        glm::vec3 latestPosition(_bugDetails[5].x, _bugDetails[5].y, _bugDetails[5].z);
        latestPosition *= 128;
        injector->setPosition(latestPosition);
        
        if (!injector->isInjectingAudio() && randIntInRange(1, 100) == 99) {
            AudioInjectionManager::threadInjector(injector);
        }
        
        // send a check in packet to the domain server if DOMAIN_SERVER_CHECK_IN_USECS has elapsed
        if (usecTimestampNow() - usecTimestamp(&lastDomainServerCheckIn) >= DOMAIN_SERVER_CHECK_IN_USECS) {
            gettimeofday(&lastDomainServerCheckIn, NULL);
            NodeList::getInstance()->sendDomainServerCheckIn();
        }
        
        // Nodes sending messages to us...
        if (nodeList->getNodeSocket()->receive(&nodePublicAddress, packetData, &receivedBytes)) {
            NodeList::getInstance()->processNodeData(&nodePublicAddress, packetData, receivedBytes);
        }
        
        // dynamically sleep until we need to fire off the next set of voxels
        long long usecToSleep =  ANIMATE_VOXELS_INTERVAL_USECS - (usecTimestampNow() - usecTimestamp(&lastSendTime));
        
        if (usecToSleep > 0) {
            usleep(usecToSleep);
        } else {
            printf("Last send took too much time, not sleeping!\n");
        }
    }
    
    printf("Removing the old bird and dying.\n");
    
    // we've been told to stop, remove the last bug
    removeOldBug();
}