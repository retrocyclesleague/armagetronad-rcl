/*

*************************************************************************

ArmageTron -- Just another Tron Lightcycle Game in 3D.
Copyright (C) 2000  Manuel Moos (manuel@moosnet.de)

**************************************************************************

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

***************************************************************************

*/

#ifndef ArmageTron_CAMERA_H
#define ArmageTron_CAMERA_H

typedef enum {CAMERA_IN=0, CAMERA_CUSTOM=1, CAMERA_FREE=2, CAMERA_FOLLOW=3,
              CAMERA_SMART=4,  CAMERA_SERVER_CUSTOM=5, CAMERA_SMART_IN=6, CAMERA_COUNT=7 } eCamMode;

#include "eCoord.h"
#include "tString.h"
//#include "uInput.h"
#include "rViewport.h"
#include "tList.h"
#include "nObserver.h"

class ePlayer;
class ePlayerNetID;
class eGameObject;
class eNetGameObject;
class eCamera;
class eGrid;
class uActionCamera;
class uActionTooltip;

//extern REAL se_cameraRise; // how far down does the current camera look?
//extern REAL se_cameraZ;

//! A view the camera can be put in, as a watcher saves them (gCamera.cpp)
//! and a played-back match will go through them.
//!  - fixed: a spot in the arena, where the eye is and where it looks;
//!  - follow: where the eye is from the cycle watched, whichever it is: in
//!    the frame of the way that rides, which turns after it at turnSpeed, or
//!    in the arena's axes, and where it looks from there.
struct eCameraView
{
    bool fixed;
    eCoord pos, dir;    //!< fixed: the eye over the floor and the way it looks
    REAL z, rise;       //!< fixed: its height, and how much it looks up per unit ahead
    REAL back, side, up;        //!< follow: behind the cycle, to its left, above it
    REAL yaw, pitch;            //!< follow: the look, turned from the frame and up per unit ahead
    REAL turnSpeed;             //!< follow: how quickly the frame turns after the cycle; 0 at once
    bool turns;                 //!< follow: the frame turns with the cycle (else the arena's axes)

    eCameraView();
    tString Serialise() const;                  //!< one line of tab separated words, no line end
    bool Parse( char const * line );            //!< what Serialise wrote; false if it is not that
};

// extern List<eCamera> se_cameras;

class eCamera{
protected:
    static uActionCamera se_lookUp,se_lookDown,se_lookLeft,se_lookRight,
    se_moveLeft,se_moveRight,se_moveUp,se_moveDown,se_moveForward,se_moveBack,
    se_zoomIn,se_zoomOut,
    se_glanceLeft,se_glanceRight,se_glanceBack,se_glanceForward,
    se_switchView;
    static uActionTooltip se_glanceLeftTooltip, se_glanceRightTooltip, se_glanceBackTooltip, se_glanceForwardTooltip, se_switchViewTooltip;

    int id;
    //  tCHECKED_PTR(eGameObject) foot;
    tCHECKED_PTR(eGrid)           grid;
    //eGameObject *center;
    nObserverPtr< ePlayerNetID>	netPlayer;
    tCHECKED_PTR(ePlayer)         localPlayer;
    // mutable nObserverPtr<eNetGameObject> center; // the game object we watch
    tJUST_CONTROLLED_PTR<eGameObject> center; // the game object we watch

    // mutable int  centerID; // the game id of the object we are watching
    //int        lastCenterID; // the game id of the object we are watching
    REAL         lastSwitch; // the last center switch
    REAL         zNear	;	 // near clipping plane
    eCamMode     mode   ;    // current view mode

    eCoord pos;       // Position
    eCoord lastPos;  // last rendered position
    eCoord dir;       // direction
    eCoord top;       // vector (top,1) is top of camera (limits views)
    REAL  z,rise;    // height above the floor and whether we look up or down
    REAL  fov;       // field of vision;

    REAL turning;   // number of turns in the last seconds
    REAL smoothTurning; //that value smoothed


    REAL distance;    // distance travelled so far
    REAL lastrendertime; // the time this was last rendered

    REAL smartcamSkewSmooth, smartcamIncamSmooth, smartcamFrontSmooth, centerSpeedSmooth;
    eCoord centerDirSmooth;
    eCoord centerDirLast;
    eCoord centerPos;
    eCoord centerPosSmooth;

    tCHECKED_PTR(rViewport) vp;

    eCoord centerPosLast;
    REAL  userCameraControl;
    REAL  centerIncam;

    bool glancingLeft,glancingRight,glancingBack,glancingForward;
    REAL glanceSmooth, glanceSmoothAbs;
    eCoord glanceDir_;

    bool renderingMain_;	// flag indicating whether the current rendering process is the main process or just a mirror effect

    bool cameraMain_;		// flag indicating whether the camera is a main camera or a widget view

    static bool InterestingToWatch(eGameObject const * g);

    eCoord Glance( eCoord const & in, eCoord const & glanceDir ) const;

    // watching without a cycle of one's own (see SpectatorFly)
    bool   spectatorFly_;       //!< the camera was handed to the watcher for this stretch of watching
    double spectatorSince_;     //!< when the watcher last had a cycle in play, or -1
    double spectatorLast_;      //!< when the flight was last stepped
    double spectatorLockUntil_; //!< until when the watcher's keys count for nothing: the moment after a cycle is lost
    unsigned spectatorHeldOver_; //!< those of the watcher's keys that have been down since then and must be let go first
    bool   spectatorWaySet_;    //!< the way the flight travels has been taken from the view
    eCoord spectatorWay_;       //!< the way the flight travels, which is not always where the view looks
    REAL   spectatorWayRise_;   //!< and how steeply up or down
    bool   spectatorFollow_;    //!< the free camera keeps the cycles in view from above until the watcher flies it

    //! what following keeps in view, and how high the camera must be to see it
    bool SpectatorTarget( eCoord & centre, REAL & height ) const;

    // a saved view in use (ApplyView)
    bool        viewFollow_;    //!< following the cycle watched as viewFollowData_ says
    eCameraView viewFollowData_;
    eCoord      viewFrame_;     //!< the way its frame faces, turning after the cycle
    void PlaceFollowView( REAL ts, eCoord & p, eCoord & d, REAL & h, REAL & r ); //!< where that puts the camera now

    // a switch of view glides from the view drawn then
    double glideStart_;
    REAL   glideTime_;          //!< 0: no glide
    eCoord glidePos_, glideDir_;
    REAL   glideZ_, glideRise_;
    bool   Gliding( double now ) const;
    void   GlidePose( double now, eCoord & p, eCoord & d, REAL & h, REAL & r ) const; //!< the view drawn at that moment

    void SpectatorFly();        //!< hands a watcher their own free camera and flies it

    void Bound( REAL dt ); //!< make sure the camera is inside the arena and has clear line of sight
    void Bound( REAL dt, eCoord & pos ); //!< make sure pos is inside the arena and has clear line of sight

    void MyInit();
public:
    bool CenterIncamOnTurn();
    bool WhobbleIncam();
    bool AutoSwitchIncam();

    bool RenderingMain() const { return renderingMain_;  }
    void SetRenderingMain( bool f ){ renderingMain_ = f; }

    bool CameraMain() const { return cameraMain_;  }
    void SetCameraMain( bool f ){ cameraMain_ = f; }

    const ePlayerNetID* Player() const;
    const ePlayer* LocalPlayer() const;

    eCamera(eGrid *grid, rViewport *vp,ePlayerNetID *owner,ePlayer *lp,eCamMode m=CAMERA_IN, bool rMain=true);
    virtual ~eCamera();

    eGameObject * Center() const;

    eCoord CenterPos() const;
    eCoord CenterDir() const;
    virtual eCoord CenterCycleDir() const;

    virtual REAL SpeedMultiplier() const { return 1; }

    eCoord CenterCamDir() const;
    eCoord CenterCamTop() const;
    eCoord CenterCamPos() const;
    REAL  CenterCamZ() const;

    REAL  CenterZ() const;
    REAL  CenterSpeed() const;

    const eCoord& CameraDir() const {return dir;}
    const eCoord& CameraPos() const {return pos;}
    eCoord        CameraGlancePos() const {return Glance(pos, glanceDir_);}
    REAL          CameraZ  () const {return z;}

    bool CenterAlive() const;

    bool CenterCockpitFixedBefore() const;
    void CenterCockpitFixedAfter() const;

    void SwitchView();
    void SwitchCenter(int d);
    bool Act(uActionCamera *act,REAL x);

    //! the main camera of a local player with no cycle of their own in play
    bool Spectating() const;
    //! the view the camera has now, without a glance; false when it follows
    //! a cycle and there is none to measure from
    bool SaveView( eCameraView & view ) const;
    //! puts the camera in the view, gliding there over that many seconds (0: a cut)
    void ApplyView( eCameraView const & view, REAL glide );

#ifndef DEDICATED
    void Render();
    void SoundMix(unsigned char *dest,unsigned int len);
private:
    void SoundMixGameObject(unsigned char *dest,unsigned int len,eGameObject *go);
public:
#endif

    virtual void Timestep(REAL ts);

    REAL Dist(){return distance;}

    /*
    static int    Number(){return se_cameras.Len();}
    static const eCoord& PosNum(int i){
      if (i<se_cameras.Len())
        return se_cameras(i)->lastPos;
      else
        return se_zeroCoord;
    }
    static const eCoord& DirNum(int i){
      if (i<se_cameras.Len())
        return se_cameras(i)->dir;
      else
        return se_zeroCoord;
    }

    static REAL HeightNum(int i){
      if (i<se_cameras.Len())
        return se_cameras(i)->z;
      else
        return 0;
    }
    */

    static void s_Timestep(eGrid *grid, REAL time);

private:
    //! make sure CenterPos() + dirFromTarget() is visible from pos
    bool Bound( REAL ratio, eCoord & pos, eCoord const & dirFromTarget, REAL & hitCache );

    enum HitCacheSize{ hitCacheSize = 3 };
    REAL hitCache_[hitCacheSize];
};

/*
inline int    NumberOfCameras(){return eCamera::Number();}
inline const eCoord& CameraPos(int i){return eCamera::PosNum(i);}
inline const eCoord& CameraDir(int i){return eCamera::DirNum(i);}
inline REAL CameraHeight(int i){return eCamera::HeightNum(i);}
*/

// the camera settings the Camera page chooses between (CAMERA_CUSTOM_TURN_SPEED,
// CAMERA_GLANCE_SNAP, CAMERA_GLANCE_HOLD, RCL_GLANCE_LIKE_PLAY)
REAL & se_CameraTurnSpeed();
bool & se_CameraGlanceSnap();
bool & se_CameraGlanceHold();
bool & se_CameraGlanceLikePlay();

#endif
