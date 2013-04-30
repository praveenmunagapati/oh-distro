

#include <iostream>
#include <cmath>

#include "TwoLegOdometry.h"

#include "TwoLegsEstimate_types.h"

#include "QuaternionLib.h"
#include <sstream>


using namespace TwoLegs;
using namespace std;

TwoLegOdometry::TwoLegOdometry(bool _log_data_files)
{
	cout << "A new TwoLegOdometry object was created" << endl;
	
	// This is just here initially for initial testing and setup of the code structure
	// TODO
	// Assuming the robot is standing still.
	//Eigen::Isometry3d first;
	footsteps.reset();
	standing_foot = -1;

	imu_orientation_estimate.setIdentity();
	
	foottransitionintermediateflag = true;
	standingintermediate = true;
	
	// TODO - expected weight must be updated from live vehicle data and not be hard coded like this
	expectedweight = 900.f;
	
	local_velocities.setZero();
	accel.setSize(3);
	
	leftforces.x = 0.f;
	leftforces.y = 0.f;
	leftforces.z = 0.f;
	
	rightforces.x = 0.f;
	rightforces.y = 0.f;
	rightforces.z = 0.f;
	
	lcmutime = 0;
	deltautime = 0;
	transition_timespan = 0;
	standing_timer = 0;
	standing_delay = 0;
	
	stepcount = 0;
	
	both_feet_in_contact = true;
	
	_left_contact_state = new SchmittTrigger(LOW_FOOT_CONTACT_THRESH, HIGH_FOOT_CONTACT_THRESH, FOOT_CONTACT_DELAY);
	_right_contact_state = new SchmittTrigger(LOW_FOOT_CONTACT_THRESH, HIGH_FOOT_CONTACT_THRESH, FOOT_CONTACT_DELAY);
	
	// the idea at this point is that if the accleration component of velocity is above the limits for 3 ms in a row the state will assume that it is infact the correct veloticy estimate
	_vel_spike_isolation[0] = new BipolarSchmittTrigger(1, 5, 3000); 
	_vel_spike_isolation[1] = new BipolarSchmittTrigger(1, 5, 3000);
	_vel_spike_isolation[2] = new BipolarSchmittTrigger(1, 5, 3000);
	
	datafile.Open(_log_data_files,"datalog.csv");
	footcontactfile.Open(_log_data_files,"footcontactlog.csv");
	accel_spike_isolation_log.Open(_log_data_files,"accel_spikes.csv");
	
	for (int i=0;i<3;i++) {temp_max_testing[i] = 0.;}
	
}

TwoLegOdometry::~TwoLegOdometry() {
	std::cout << "Terminating a TwoLegOdometry object and its allocated memory\n";
	
	terminate();
	
	delete _left_contact_state;
	delete _right_contact_state;
	delete _vel_spike_isolation[0];
	delete _vel_spike_isolation[1];
	delete _vel_spike_isolation[2];
		
}

void TwoLegOdometry::parseRobotInput() {
	
  cout << "TwoLegOdometry::parseLCMInput() called, NOT implemented" << endl;
  
  return;
}

/*
 * Updates the internal states of the odometry object and under the correct circumstances transistions the standing foot as the robot is walking
*/
void TwoLegOdometry::CalculateBodyStates_Testing(int counter) {
	cout << "_leg_odo::CalculateBodyStates() NOT implemented yet" << endl;
	
	// calculate the new pelvis and head states
	// create some data and pass it to this function
	
	/*data = ? */
	
	CalculateBodyStates(/*data*/);
	
	return;
}

int TwoLegOdometry::secondary_foot() {
	if (standing_foot == LEFTFOOT)
		return RIGHTFOOT;
	if (standing_foot == RIGHTFOOT)
		return LEFTFOOT;
	
}

void TwoLegOdometry::CalculateBodyStates(/*data*/) {
	
	cout << "TwoLegOdometry::CalculateBodyStates() NOT IMPLEMENTED YET" << endl << endl;
	
	
	return;
}



// now this seems excessive and should maybe be a more general transform update thing used for legs and head and pelvis
state TwoLegOdometry::getSecondaryFootState() {
	state secondaryfoot_state;
	
	//secondaryfoot_state = ??
	
	return secondaryfoot_state;
}

void TwoLegOdometry::setStandingFoot(int foot) {
	standing_foot = foot;
}

// TODO - unused function, to be depreciated
void TwoLegOdometry::updateInternalStates(/*data*/) {
	cout << "void TwoLegOdometry::updateInternalStates(); nothing updated - NOT implemented" << endl;
	
	// This function is currently not used
	
	return;
}

int TwoLegOdometry::primary_foot() {
	return standing_foot;
}

footstep TwoLegOdometry::DetectFootTransistion(int64_t utime, float leftz, float rightz) {
	
	deltautime =  utime - lcmutime;
	lcmutime = utime;
	leftforces.z = leftz;
	rightforces.z = rightz;
	
	footstep newstep;
	newstep.foot = -1;
	
	stringstream ss (stringstream::in | stringstream::out);
	stringstream cnct_est (stringstream::in | stringstream::out);

	
	if (getSecondaryFootZforce() - SCHMITT_LEVEL*expectedweight > getPrimaryFootZforce()) {
	  transition_timespan += deltautime;
	}
	else
	{
	  transition_timespan = 0.;
	  foottransitionintermediateflag = true;
	  
	  // in the intermediate zone
	  // potentially standing
	}
	  
	if (transition_timespan > TRANSITION_TIMEOUT && foottransitionintermediateflag) 
	{
		Eigen::Isometry3d transform;
		
		
		Eigen::Isometry3d temp;
		temp = getSecondaryInLocal();
#ifdef VERBOSE_DEBUG
		std::cout << "Adding footstep with values: " << temp.translation().transpose() << std::endl;
#endif

	  foottransitionintermediateflag = false;
	  
	  std::cout << "NEW STEP ON " << ((secondary_foot()==LEFTFOOT) ? "LEFT" : "RIGHT") << " stepcount: " << stepcount << " at x= " << getSecondaryInLocal().translation().x() << std::endl;

	  stepcount++;
	  newstep.foot = secondary_foot();
	  //newstep.footprintlocation = getSecondaryInLocal();
	  newstep.footprintlocation = AccumulateFootPosition(getPrimaryInLocal(), primary_foot());
	  
	  
	  // TODO - investigate this large delay requirement and tie it to a proper requirements, rather than a fudge factor
  	  standing_delay = 5*STANDING_TRANSITION_TIMEOUT;
	  
	} else {

		
			double loadsplit;
			
			loadsplit = abs(leftz*leftz - rightz*rightz) < (LOADSPLIT_LEVEL*expectedweight*LOADSPLIT_LEVEL*expectedweight);
			
			// separation requirement no longer needed, kept for future reference
			//separation = abs(pelvis_to_left.translation().x()) + abs(pelvis_to_right.translation().x());
			//&& separation < MIN_STANDING_FEET_X_SEP
			
			// Second layer of logic testing to isolate the robot standing condition
			if (loadsplit && leftz > MIN_STANDING_FORCE && rightz > MIN_STANDING_FORCE) {
				
				if (standing_delay > 0) {
					standing_delay -= deltautime;
					//std::cout << "reducing standing delay to: " << standing_delay << std::endl;
				} else {
					standing_delay = 0;
				}
				
				standing_timer += deltautime;
			}
			else
			{
				if (standing_timer>STANDING_TRANSITION_TIMEOUT)
				{
					standing_timer = STANDING_TRANSITION_TIMEOUT;
					standingintermediate = true;
				}
				standing_timer -= deltautime;
				if (standing_timer<0) {
					standingintermediate = false;
					standing_timer = 0;
					
					both_feet_in_contact = false;
				}
				else
				{
					;
				}
			}
			
			if ((standing_timer > STANDING_TRANSITION_TIMEOUT && standing_delay<=0) || standingintermediate) {
				//std::cout << "Standing for: " << standing_timer <<  "\n";
				both_feet_in_contact = true;
			}
			else
			{
				;
			}
			
	}
	
#if defined( LOG_DATA_FILES )
	ss << leftforces.z << ", " << rightforces.z << ", ";
	
	ss << standing_timer << ", " << standing_delay << ", ";
	ss << ((standingintermediate) ? "1" : "0") << ", ";
	ss << ((both_feet_in_contact) ? "1" : "0");
	
	ss << std::endl;
	string datastr = ss.str();
	datafile.log(datastr);
	
	cnct_est << leftforces.z << ", " << rightforces.z << ", ";
	
	cnct_est << ( leftContactStatus() > 0.5 ? "1" : "0") << ", ";
	cnct_est << (rightContactStatus() > 0.5 ? "1" : "0");
	cnct_est << "\n";
	
	datastr = "";
	datastr = cnct_est.str();
	
	footcontactfile.log(datastr);
#endif
	
	
	return newstep;
}

bool TwoLegOdometry::FootLogic(long utime, float leftz, float rightz) {
  footstep newstep;
  newstep = DetectFootTransistion(utime, leftz, rightz);
  
  if (newstep.foot == LEFTFOOT || newstep.foot == RIGHTFOOT) {
	  std::cout << "FootLogic adding Footstep " << (newstep.foot == LEFTFOOT ? "LEFT" : "RIGHT") << std::endl;
	  standing_foot = newstep.foot;
	  // footsteps have the correct yaw angle when they go into the footstep list -- positive is CCW, positive around +Z
	  footsteps.newFootstep(newstep);
	  return true;
  }
  return false;
}

float TwoLegOdometry::getPrimaryFootZforce() {
  if (standing_foot == LEFTFOOT)
    return leftforces.z;
  return rightforces.z;
}

float TwoLegOdometry::getSecondaryFootZforce() {
	if (standing_foot == LEFTFOOT)
	    return rightforces.z;
	  return leftforces.z;
}

void TwoLegOdometry::setLegTransforms(const Eigen::Isometry3d &left, const Eigen::Isometry3d &right) {
	pelvis_to_left = left;
	pelvis_to_right = right;
	
	//std::cout << "Setting leg transforms\n";
	
	// TODO - do the rotation assignments have not been tested yet
	left_to_pelvis.translation() = -left.translation();
	left_to_pelvis.linear() = left.linear().transpose();
	right_to_pelvis.translation() = -right.translation();
	right_to_pelvis.linear() = right.linear().transpose();
	
	
}

void TwoLegOdometry::setOrientationTransform(const Eigen::Quaterniond &ahrs_orientation, const Eigen::Vector3d &body_rates) {
	
	Eigen::Matrix3d C;
	
	C = InertialOdometry::QuaternionLib::q2C(ahrs_orientation);
	
	//imu_orientation_estimate = InertialOdometry::QuaternionLib::C2q(C.transpose());// think there is a problem with this function 
	imu_orientation_estimate = ahrs_orientation;
	
	//ComputeLocalOrientation();
	
	//Eigen::Vector3d local_rpy_rate = local_to_body_.linear() * ( temp_body_rpy_rate );
	
	Eigen::Quaterniond yaw_q;
	//yaw_q = InertialOdometry::QuaternionLib::C2q(getPelvisFromStep().linear()); // This may be the problem child right here
	//std::cout << "Yaw is being set to: " << InertialOdometry::QuaternionLib::C2e(getPelvisFromStep().linear())(2) << std::endl;
	yaw_q = InertialOdometry::QuaternionLib::e2q(InertialOdometry::QuaternionLib::C2e(getPelvisFromStep().linear()));
	//std::cout << "Quat is: " << yaw_q.w() << ", " << yaw_q.x() << ", " << yaw_q.y() << ", " << yaw_q.z() << std::endl;
	//std::cout << "Yaw is being set to:\n" << getPelvisFromStep().linear() << "\n";
	local_frame_orientation = MergePitchRollYaw(imu_orientation_estimate,yaw_q);
	
	local_frame_rates = InertialOdometry::QuaternionLib::q2C(local_frame_orientation) * body_rates;
}

Eigen::Vector3d const TwoLegOdometry::getLocalFrameRates() {
	return local_frame_rates;
}

Eigen::Isometry3d TwoLegOdometry::getSecondaryFootToPelvis() {
	//std::cout << "Taking primary as: " << (footsteps.lastFoot()==LEFTFOOT ? "LEFT" : "RIGHT") << std::endl;
	if (footsteps.lastFoot() == LEFTFOOT)
		return right_to_pelvis;
	if (footsteps.lastFoot() == RIGHTFOOT)
		return left_to_pelvis;
	return Eigen::Isometry3d();
}

Eigen::Isometry3d TwoLegOdometry::getPrimaryFootToPelvis() {
	if (footsteps.lastFoot() == LEFTFOOT)
		return left_to_pelvis;
	if (footsteps.lastFoot() == RIGHTFOOT)
		return right_to_pelvis;
	return Eigen::Isometry3d();
}

Eigen::Quaterniond TwoLegOdometry::MergePitchRollYaw(const Eigen::Quaterniond &q_RollPitch, const Eigen::Quaterniond &q_Yaw) {
	Eigen::Vector3d E_rp;
	Eigen::Vector3d E_y;
		
	// TODO -- Remove the dependence on gimbal lock, by not using the Euler angle representation when merging the attitude angle estimates from the different computations
	
	
	E_rp = InertialOdometry::QuaternionLib::q2e(q_RollPitch);
	E_y  = InertialOdometry::QuaternionLib::q2e(q_Yaw);
	
	
	Eigen::Quaterniond return_q;
	
	//E_rp(1) = 1.;//its drawing what i expect, but requires a transpose on the .linear() from Eigen::Isometry3d testing higher up..
	
	//std::cout << "Roll and Pitch and yaw from ahrs: " << E_rp.transpose() << std::endl;
	
	// Only use the yaw angle from the leg kinematics
	E_y(0) = 0.;
	E_y(1) = 0.;
	
	//use pitch and roll form the IMU states which are read directly from the LCM TORSO_IMU message -- This is assumed to be a Kalman Filter based AHRS value
	E_rp(2) = 0.;
	
	//std::cout << "Merge: " << (E_lk + E_imu).transpose() << std::endl;
	
	Eigen::Vector3d output_E;
	output_E = (E_rp + E_y);
	
	//Eigen::Vector3d output_E( 0.,0.,0.);
	
	//std::cout << "Set E to: " << output_E.transpose() << std::endl;
	//std::cout << output_E(2) << std::endl;
	
	return_q = InertialOdometry::QuaternionLib::e2q(output_E);
	
	return return_q;
}


// The intended user member call to get the pelvis state. The orientation is a mix of the leg kinematics yaw and the torso IMU AHRS pitch and roll angles
// Translation is from the accumulated leg kinematics
Eigen::Isometry3d TwoLegOdometry::getPelvisState() {
	
	Eigen::Isometry3d output_state(local_frame_orientation);
	output_state.translation() = getPelvisFromStep().translation();
	
	return output_state;
}	

Eigen::Vector3d TwoLegOdometry::getPelvisVelocityStates() {
	
	//std::cout << "TwoLegOdometry::getPelvisVelocityStates() IS NOT READY TO BE USED\n";
	
	return local_velocities;
}

Eigen::Isometry3d TwoLegOdometry::getPelvisFromStep() {
	//std::cout << "Last step: " << footsteps.getLastStep().translation().transpose() << "\n";
	//std::cout << "Primary to Pelvis: " << getPrimaryFootToPelvis().translation().transpose() << "\n";
	//std::cout << "Add: " << add(footsteps.getLastStep(),getPrimaryFootToPelvis()).translation().transpose() << "\n";
	
	Eigen::Isometry3d returnval;
	returnval = add(footsteps.getLastStep(),getPrimaryFootToPelvis());
	//std::cout << "Returning yaw:\n" << InertialOdometry::QuaternionLib::C2e(returnval.linear())(2) << std::endl;
	//std::cout << "Returning linear:\n" << returnval.linear() << "\n";
	
	return returnval;
}

Eigen::Isometry3d TwoLegOdometry::AccumulateFootPosition(const Eigen::Isometry3d &from, const int foot_id) {
	Eigen::Isometry3d returnval;
	returnval.translation() << -99999999999., -99999999999.,-99999999999.;
	
	switch (foot_id) {
	case LEFTFOOT:
		returnval = add(add(from,left_to_pelvis),pelvis_to_right);
		break;
	case RIGHTFOOT:
		//std::cout << "Going right: " << getPrimaryInLocal().translation().x() << ", " << right_to_pelvis.translation().x() << ", " << pelvis_to_left.translation().x() << std::endl;
		returnval = add(add(from,right_to_pelvis),pelvis_to_left);
		break;
	default:
		std::cout << "THIS SHOULD NEVER HAPPEN - TwoLegOdometry::AccumulateFootPosition()" << std::endl;
		break;
	}
	
	return returnval;
}

Eigen::Isometry3d TwoLegOdometry::getSecondaryInLocal() {
	
	//std::cout << "pelvis to left: " << (pelvis_to_left*local_to_pelvis).translation().transpose() << std::endl;
	//std::cout << (secondary_foot()==LEFTFOOT ? "LEFT" : "RIGHT") << " is secondary_foot()" << std::endl;
	Eigen::Isometry3d returnval;
	returnval = AccumulateFootPosition(getPrimaryInLocal(),primary_foot());
	
	return returnval;
}

Eigen::Isometry3d TwoLegOdometry::getPrimaryInLocal() {
	return footsteps.getLastStep();
	
}

Eigen::Isometry3d TwoLegOdometry::getLeftInLocal() {
	return add(getPelvisFromStep(), pelvis_to_left);
}


Eigen::Isometry3d TwoLegOdometry::getRightInLocal() {
	return add(getPelvisFromStep(), pelvis_to_right);
}

void TwoLegOdometry::setPelvisPosition(Eigen::Isometry3d transform) {
	local_to_pelvis = transform;
}


Eigen::Isometry3d TwoLegOdometry::add(const Eigen::Isometry3d& lhs, const Eigen::Isometry3d& rhs) {
	Eigen::Isometry3d add;
	
	/*
	 * What we did previously
	 * 
	add.translation() = lhs.translation() + InertialOdometry::QuaternionLib::Cyaw_rotate(lhs.linear() ,InertialOdometry::QuaternionLib::Cyaw_rotate(rhs.linear(),rhs.translation()));
	add.linear() = lhs.linear() * rhs.linear();
	*/
	
	// Using the Isometry operator directly
	add = lhs*rhs;
	
	return add;
}

void TwoLegOdometry::ResetInitialConditions(const Eigen::Isometry3d &left_, const Eigen::Isometry3d &init_states) {
	// The left foot is used to initialise height of the pelvis.
	
	/* This will probably be depreciated soon
	Eigen::Vector3d zero;
	zero << 0.,0.,-left_.translation().z();
	local_to_pelvis.translation() = zero;
	local_to_pelvis.linear().setIdentity();
	*/
	
	stepcount = 0;
	local_to_pelvis = init_states;
	
	footsteps.reset();
}

void TwoLegOdometry::ResetWithLeftFootStates(const Eigen::Isometry3d &left_, const Eigen::Isometry3d &right_, const Eigen::Isometry3d &init_states) {
	
	ResetInitialConditions(left_, init_states);
	
	//std::cout << "Pelvis was set to: " << local_to_pelvis.translation().transpose() << std::endl;
	//std::cout << "Last step location before add: " << footsteps.getLastStep().translation().transpose() << std::endl;
	//std::cout << "PrimaryFoot to Pelvis: " << getPrimaryFootToPelvis().translation().transpose() << std::endl;
	
	footsteps.addFootstep(add(local_to_pelvis,left_),LEFTFOOT);
	//std::cout << "Last step location after add: " << footsteps.getLastStep().translation().transpose() << std::endl;
	//footsteps.addFootstep(pelvis_to_left,LEFTFOOT);
	standing_foot = LEFTFOOT; // Not sure that double states should be used, this needs to change TODO
}

int TwoLegOdometry::getStepCount() {
	return stepcount;
}

int TwoLegOdometry::getActiveFoot() {
	return standing_foot;
}

void TwoLegOdometry::updateSingleFootContactStates(long utime, const double left_force, const double right_force) {
	_left_contact_state->UpdateState(utime, left_force);
	_right_contact_state->UpdateState(utime, right_force);
}


float TwoLegOdometry::leftContactStatus() {
	return _left_contact_state->getState();
	
	/*
	 * This comes from when the contact state classification was done from the odometry classification
	 * This has been changed since weight baring and contact states have developed to be a little different.
	 * We therefore maintain two classifiers
	 * 
	if (getActiveFoot() == LEFTFOOT || both_feet_in_contact) {
		return 1.0f;
	}
	return 0.0f;*/
}

float TwoLegOdometry::rightContactStatus() {
	return _right_contact_state->getState();
	
	/*
	if (getActiveFoot() == RIGHTFOOT || both_feet_in_contact) {
		return 1.0f;
	}
	return 0.0f;
	*/
}

void TwoLegOdometry::terminate() {
	std::cout << "Terminating and cleaning out TwoLegOdometry object\n";
	
	datafile.Close();
	footcontactfile.Close();
	accel_spike_isolation_log.Close();
}

void TwoLegOdometry::calculateUpdateVelocityStates(int64_t current_time) {
	//std::cout << "Not implemented yet\n";
	
	Eigen::Vector3d current_position;
	//Eigen::Vector3d velocity_estimate;
	
	current_position = getPelvisState().translation();
	
	Eigen::Vector3d prev_velocities;
	prev_velocities = local_velocities;
	local_velocities = (1.e6)*(current_position - previous_isometry.translation())/(current_time - previous_isometry_time);
	
	
	// This is to ignore velocity spikes that occur on when the active foot state is transitioned from left to right
	// This is probablydue to some uncompensated offset at the present time, but may be good enough for the present requirements
	// if the velocity which is calculated here is high for a period, we need to consider that something has gone wrong.
	// But there is a known issue with transitioins between feet, whic create a large velocity spike.
	// Suggested cure is a delayed Schmitt trigger once more -- this is to pass or ignore spikes with a time delay.
	local_accelerations = accel.diff((1.E-6)*current_time, local_velocities);
	stringstream accel_data_ss (stringstream::in | stringstream::out);
	
	accel_data_ss << local_accelerations(0) << ", "  << local_accelerations(1) << ", " << local_accelerations(2) << ", ";
	
	
	
	for (int i=0;i<3;i++) {
		_vel_spike_isolation[i]->UpdateState(current_time, local_accelerations(i));
		
		accel_data_ss << !_vel_spike_isolation[i]->getState()  << ", ";
		
		if (local_accelerations(i) < -4 || local_accelerations(i) > 4)
		{
			if (!_vel_spike_isolation[i]->getState()) {
				
				// accel values have not remained high, and can therefore be ignored
				local_velocities(i) = prev_velocities(i);
			}
			// else do noting and the current computed state will not be ignored
		}
	}
	
	accel_data_ss << local_velocities(0) << ", " << local_velocities(1) << ", " << local_velocities(2);

	accel_data_ss << std::endl;
	accel_spike_isolation_log << accel_data_ss.str();
	
	previous_isometry = getPelvisState();
	previous_isometry_time = current_time;
}
