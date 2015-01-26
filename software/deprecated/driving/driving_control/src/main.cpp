#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <glib.h>
#include <sys/time.h>
#include <getopt.h>
#include <string.h>
#include <unistd.h>

#ifdef __APPLE
#include <OpenGL/gl.h>
#else
#include <GL/gl.h>
#endif


#include <lcm/lcm.h>

#include <bot_core/bot_core.h>
#include <bot_param/param_client.h>
#include <bot_frames/bot_frames.h>
#include <bot_lcmgl_client/lcmgl.h>
#include <occ_map/PixelMap.hpp>

#include <lcmtypes/occ_map_pixel_map_t.h>
#include <lcmtypes/drc_driving_control_params_t.h>
#include <lcmtypes/drc_driving_control_cmd_t.h>
#include <lcmtypes/drc_utime_t.h>
#include <lcmtypes/drc_driving_cmd_t.h>
#include <lcmtypes/drc_driving_controller_status_t.h>
#include <lcmtypes/perception_pointing_vector_t.h>
#include <lcmtypes/drc_system_status_t.h>
#include <lcmtypes/drc_driving_controller_values_t.h>
#include <lcmtypes/drc_driving_affordance_status_t.h>
#include <vector>
#include <algorithm> 

#define DEFAULT_GOAL_DISTANCE 10.0

#define TIMER_PERIOD_MSEC 50

#define STATUS_TIMER_PERIOD_MSEC 1000
#define STATE_UPDATE_TIMER_PERIOD_MSEC 1000

//#define TIME_TO_TURN_PER_RADIAN 1.0

//this is a hack for now - adding a fixed time to turn 
#define TIME_TO_TURN 1.0
#define TIME_TO_BRAKE 2.0

#define MAX_BRAKE 0.5

#define TIMER_PERIOD_MSEC_LONG 700

#define STOP_TIME_GAP_SEC 50.0
#define ACCELERATOR_TIME_GAP_SEC 2.5
#define TIMEOUT_THRESHOLD 1.0
#define MAX_THROTTLE 0.03
#define ALPHA .2
#define VEHICLE_THRESHOLD_ARC 1/2.0
#define VEHICLE_THRESHOLD 1/1.5
#define ESTOP_VEHICLE_THRESHOLD 1/1.2

#define SAFE_DISTANCE 3.0

#define MIN_SCAN_DIST 6.0

#define DIST_POW 2

#define TLD_TIMEOUT_SEC 1.0

#define STEERING_RATIO 0.063670 //0.125 - might be the new value 

#define CAR_FRAME "body" // placeholder until we have the car frame

typedef enum {
    IDLE, ERROR_NO_MAP, ERROR_MAP_TIMEOUT, DRIVING_ROAD_ONLY_CARROT, DRIVING_ROAD_ONLY_ARC, DOING_INITIAL_TURN, 
    DOING_BRAKING, DRIVING_TLD_AND_ROAD, DRIVING_TLD, DRIVING_USER, ERROR_TLD_TIMEOUT, ERROR_NO_VALID_GOAL
} controller_state_t;


typedef struct{
    double xy[2];
    double score;
} pos_t;

typedef struct{
    double xy[2];
} xy_t;

typedef struct{
    pos_t pos;
    double steering_angle;
    std::vector<xy_t> sample_points;
} steering_goal_t;


typedef struct _state_t {
    lcm_t *lcm;
    BotParam * param;
    BotFrames * frames;
    GMainLoop *mainloop; 
    guint controller_timer_id;
    int64_t last_controller_utime;
    controller_state_t curr_state;
  
    occ_map_pixel_map_t *cost_map_last;
    perception_pointing_vector_t *tld_bearing;
    
    drc_driving_cmd_t *last_driving_cmd; 

    bot_lcmgl_t *lcmgl_goal; 
    bot_lcmgl_t *lcmgl_arc; 
    bot_lcmgl_t *lcmgl_rays; 
    int64_t utime;

    int use_differential_angle; 
    double timer_period; 

    int do_braking;
    int64_t time_applied_to_brake;
    
    int64_t time_applied_to_accelerate;

    double max_steering;
    double min_steering; 

    double max_throttle;
    double min_throttle;
  
    double throttle_duration;
    double throttle_ratio;

    double drive_duration; 
    int64_t drive_start_time;

    int64_t drive_time_to_go;

    double error_last; 
    
    double goal_distance;
    
    double actual_goal_distance;
    double goal_heading;
    double kp_steer;
    double kd_steer;

    int have_valid_goal;
    double cur_goal[3];
  
    int accelerator_utime; 
    int stop_utime;
    // Control parameters

    double throttle_val;
    double brake_val;
    double hand_steer;

    int verbose;
} state_t;

void publish_system_state_values(state_t *self){
    drc_driving_controller_values_t msg;
    msg.utime = self->utime;
    msg.throttle_value = (self->throttle_val) * 100;
    msg.brake_value = (self->brake_val)*100;
    msg.hand_steer = bot_to_degrees(self->hand_steer);
    msg.goal_distance = self->actual_goal_distance * 10;
    msg.goal_heading_angle = bot_to_degrees(self->goal_heading);
    if(self->drive_duration >=0){
        if(self->do_braking){
            if(self->drive_time_to_go/1.0e6 > self->drive_duration)
                msg.time_to_drive = self->drive_duration;
            else
                msg.time_to_drive = (self->drive_time_to_go/1.0e6 - TIME_TO_BRAKE) * 10 ;
        }
        else{
            if(self->drive_time_to_go/1.0e6 > self->drive_duration)
                msg.time_to_drive = self->drive_duration;
            else
                msg.time_to_drive = (self->drive_time_to_go/1.0e6) * 10;
        }
    }
    else{
        msg.time_to_drive = 0;
    }

    fprintf(stderr, "Time to drive : %f => %d - %f\n", self->drive_time_to_go/1.0e6, msg.time_to_drive, msg.time_to_drive / 10.0);
    drc_driving_controller_values_t_publish(self->lcm, "DRIVING_CONTROLLER_COMMAND_VALUES", &msg);
}

void publish_status(state_t *self){
    drc_driving_controller_status_t msg;
    msg.utime = self->utime;
    if(self->drive_duration >=0){
        if(self->do_braking){
            msg.time_to_drive = self->drive_time_to_go - TIME_TO_TURN * 1e6 - TIME_TO_BRAKE * 1e6;
        }
        else{
            msg.time_to_drive = self->drive_time_to_go - TIME_TO_TURN * 1e6;
        }
    }
    else{
        msg.time_to_drive = 0;
    }

    char status[1024];
    switch(self->curr_state){

    case IDLE:
        msg.status = DRC_DRIVING_CONTROLLER_STATUS_T_IDLE;
        //s_msg.value = "IDLE";
        break;
    case ERROR_NO_MAP:
        msg.status = DRC_DRIVING_CONTROLLER_STATUS_T_ERROR_NO_MAP;
        break;
    case ERROR_MAP_TIMEOUT:
        msg.status = DRC_DRIVING_CONTROLLER_STATUS_T_ERROR_MAP_TIMEOUT;
        break;
    case DRIVING_ROAD_ONLY_CARROT:
        msg.status = DRC_DRIVING_CONTROLLER_STATUS_T_DRIVING_ROAD_ONLY_CARROT;
        break;
    case DRIVING_ROAD_ONLY_ARC:
        msg.status = DRC_DRIVING_CONTROLLER_STATUS_T_DRIVING_ROAD_ONLY_ARC;
        break;
    case DRIVING_TLD_AND_ROAD:
        msg.status = DRC_DRIVING_CONTROLLER_STATUS_T_DRIVING_TLD_AND_ROAD;
        break;
    case DRIVING_TLD:
        msg.status = DRC_DRIVING_CONTROLLER_STATUS_T_DRIVING_TLD;
        break;
    case DRIVING_USER:
        msg.status = DRC_DRIVING_CONTROLLER_STATUS_T_DRIVING_USER;
        break;
    case ERROR_TLD_TIMEOUT:
        msg.status = DRC_DRIVING_CONTROLLER_STATUS_T_ERROR_TLD_TIMEOUT;
        break;
    case ERROR_NO_VALID_GOAL:
        msg.status = DRC_DRIVING_CONTROLLER_STATUS_T_ERROR_NO_VALID_GOAL;
        break;
    case DOING_INITIAL_TURN:
        msg.status = DRC_DRIVING_CONTROLLER_STATUS_T_DRIVING_DOING_INITIAL_TURN;
        break;
    case DOING_BRAKING:
        msg.status = DRC_DRIVING_CONTROLLER_STATUS_T_DRIVING_DOING_BRAKING;
        break;

    default:
        break;
    }
    drc_driving_controller_status_t_publish(self->lcm, "DRC_DRIVING_CONTROLLER_STATUS", &msg);
    //drc_system_status_t_publish(self->lcm, "SYSTEM_STATUS", &s_msg);
}

static void on_driving_affordance_status(const lcm_recv_buf_t * buf, const char *channel, 
                                         const drc_driving_affordance_status_t *msg, void *user){
    state_t *self = (state_t *) user;

    if (!strcmp (channel, "DRIVING_STEERING_ACTUATION_STATUS")) {
        double new_min = fmin(msg->dof_value_0, msg->dof_value_1);
        double new_max = fmax(msg->dof_value_0, msg->dof_value_1);
        if (msg->have_manip_map) {
            self->max_steering = new_min;
            self->min_steering = new_max;
	}       
    } else if (!strcmp (channel, "DRIVING_GAS_ACTUATION_STATUS")) {
        double new_min = fmin(msg->dof_value_0, msg->dof_value_1);
        double new_max = fmax(msg->dof_value_0, msg->dof_value_1);
        if (msg->have_manip_map) {
            self->max_throttle = new_min;
            self->min_throttle = new_max;
        }
    }
 
    return;
}

void publish_system_status(state_t *self){
    
    drc_system_status_t s_msg;
    s_msg.utime = self->utime;
    s_msg.system = DRC_SYSTEM_STATUS_T_DRIVING;
    s_msg.importance = DRC_SYSTEM_STATUS_T_VERY_IMPORTANT;
    s_msg.frequency = DRC_SYSTEM_STATUS_T_LOW_FREQUENCY;
    
    double time_to_drive = 0;
    if(self->drive_duration >=0){
        if(self->do_braking){
            time_to_drive = self->drive_time_to_go - TIME_TO_TURN * 1e6 - TIME_TO_BRAKE * 1e6;
        }
        else{
            time_to_drive = self->drive_time_to_go - TIME_TO_TURN * 1e6;
        }
    }
    char status[1024];
    switch(self->curr_state){

    case IDLE:
        s_msg.value = "IDLE";
        break;
    case ERROR_NO_MAP:
        s_msg.value = "NO_MAP";
        break;
    case ERROR_MAP_TIMEOUT:
        s_msg.value = "MAP_TIMEOUT";
        break;
    case DRIVING_ROAD_ONLY_CARROT:
        sprintf(status, "DRIVING_ROAD_ONLY_CARROT : %.2f", time_to_drive/1.0e6);
        s_msg.value = status;
        break;
    case DRIVING_ROAD_ONLY_ARC:
        sprintf(status, "DRIVING_ROAD_ONLY_ARC : %.2f", time_to_drive/1.0e6);
        s_msg.value = status;
        break;
    case DRIVING_TLD_AND_ROAD:
        sprintf(status, "DRIVING_TLD_AND_ROAD : %.2f", time_to_drive/1.0e6);
        s_msg.value = status;
        break;
    case DRIVING_TLD:
        sprintf(status, "DRIVING_TLD : %.2f", time_to_drive/1.0e6);
        s_msg.value = status;
        break;
    case DRIVING_USER:
        s_msg.value = "DRIVING_USER";
        break;
    case ERROR_TLD_TIMEOUT:
        s_msg.value = "ERROR_TLD_HEADING_TIMEOUT";
        break;
    case ERROR_NO_VALID_GOAL:
        s_msg.value = "ERROR_NO_VALID_GOAL";
        break;
    case DOING_INITIAL_TURN:
        s_msg.value = "DOING_INITIAL_TURN";
        break;
    case DOING_BRAKING:
        s_msg.value = "DOING_BRAKING";
        break;
    default:
        s_msg.value = "ERROR - UNKNOWN STATUS";
        break;
    }
    drc_system_status_t_publish(self->lcm, "SYSTEM_STATUS", &s_msg);
}

void draw_goal(state_t *self){
    if (self->lcmgl_goal) {
        bot_lcmgl_t *lcmgl = self->lcmgl_goal; 

        double xyz_car_car[] = {0, 0, 0};
        double xyz_car_local[3];

        bot_frames_transform_vec (self->frames, CAR_FRAME, "local", xyz_car_car, xyz_car_local);
        xyz_car_local[2] = 0.0;

        lcmglColor3f (0.0, 0.0, 1.0);
        lcmglCircle (xyz_car_local, self->goal_distance);

        bot_lcmgl_line_width(lcmgl, 5);
        lcmglColor3f(1.0, 0.0, 0.0);

        lcmglCircle(self->cur_goal, 0.6);
        bot_lcmgl_switch_buffer(self->lcmgl_goal);
    }  
}

static bool score_compare(const std::pair<int, pos_t>& lhs, const std::pair<int, pos_t>& rhs) { 
    return lhs.second.score < rhs.second.score;
}

static bool steering_score_compare(const steering_goal_t &lhs, const steering_goal_t& rhs) { 
    return lhs.pos.score < rhs.pos.score;
}


void draw_goal_range (state_t *self) 
{
    if (self->lcmgl_goal) {
        double xyz_car_car[] = {0, 0, 0};
        double xyz_car_local[3];

        bot_frames_transform_vec (self->frames, CAR_FRAME, "local", xyz_car_car, xyz_car_local);
        xyz_car_local[2] = 0.0;

        bot_lcmgl_t *lcmgl = self->lcmgl_goal;
        lcmglColor3f (1.0, 0.0, 0.0);
        lcmglCircle (xyz_car_local, self->goal_distance);
        bot_lcmgl_switch_buffer (self->lcmgl_goal);
    }
}

static void
on_utime (const lcm_recv_buf_t *rbuf, const char *channel,
          const drc_utime_t *msg, void *user)
{
    state_t *self = (state_t *) user;
    self->utime = msg->utime;
  
}

static void
on_bearing_vec (const lcm_recv_buf_t *rbuf, const char *channel,
                const perception_pointing_vector_t *msg, void *user)
{
    state_t *self = (state_t *) user;
    if(self->tld_bearing){
        perception_pointing_vector_t_destroy(self->tld_bearing);
    }
    self->tld_bearing =  perception_pointing_vector_t_copy(msg);

    //this is in body frame 
    float vx = self->tld_bearing->vec[0], vy = self->tld_bearing->vec[1]; // , vz = self->tld_bearing->vec[2];
    double utime = self->tld_bearing->utime; 

    printf("bearing vec: %3.2f %3.2f\n", vx, vy);
}

static void
on_driving_command (const lcm_recv_buf_t *rbuf, const char *channel,
                    const drc_driving_cmd_t *msg, void *user)
{
    state_t *self = (state_t *) user;
    fprintf(stderr, "Drive Command received\n");

    if(self->last_driving_cmd){
        drc_driving_cmd_t_destroy(self->last_driving_cmd);
    }

    self->last_driving_cmd = drc_driving_cmd_t_copy(msg);
    
    if(msg->type == DRC_DRIVING_CMD_T_TYPE_USE_ROAD_LOOKAHEAD_CARROT_B || msg->type == DRC_DRIVING_CMD_T_TYPE_USE_ROAD_LOOKAHEAD_ARC_B || msg->type == DRC_DRIVING_CMD_T_TYPE_USE_TLD_LOOKAHEAD_WITH_ROAD_B || msg->type == DRC_DRIVING_CMD_T_TYPE_USE_TLD_LOOKAHEAD_IGNORE_ROAD_B || msg->type == DRC_DRIVING_CMD_T_TYPE_USE_USER_HEADING_B){
        self->do_braking = 1;
    }
    else{
        self->do_braking = 0;
    }

    if(self->do_braking){
        self->drive_duration = msg->drive_duration/10.0 + TIME_TO_TURN + TIME_TO_BRAKE; 
    }
    else{
        self->drive_duration = msg->drive_duration/10.0 + TIME_TO_TURN; 
    }
    fprintf(stderr, " ++++ Drive duration +++ : %d -> %f\n", msg->drive_duration, self->drive_duration);

    self->kp_steer = msg->kp_steer;
    self->kd_steer = 0;//msg->kd_steer;
    self->throttle_ratio = msg->throttle_ratio/ 100.0;
    self->throttle_duration = msg->throttle_duration/ 10.0;
    self->goal_distance = msg->lookahead_dist / 10.0;
    self->drive_start_time = self->utime;
    self->time_applied_to_accelerate  = 0;    

    self->error_last = 0;
}

static void
on_driving_control_params (const lcm_recv_buf_t *rbuf, const char *channel,
                           const drc_driving_control_params_t *msg, void *user)
{

    state_t *self = (state_t *) user;

    if (self->verbose) {
        fprintf (stdout, "Changing control parameters: lookahead_dist from %.2f --> %.2f\n",
                 self->goal_distance, msg->lookahead_distance);
        fprintf (stdout, "                             kp_steer from       %.2f --> %.2f\n",
                 self->kp_steer, msg->kp_steer);
    }

    self->goal_distance = msg->lookahead_distance;
    self->kp_steer = msg->kp_steer;
}


static void
on_terrain_dist_map (const lcm_recv_buf_t *rbuf, const char *channel,
                     const occ_map_pixel_map_t *msg, void *user)
{

    state_t *self = (state_t *) user;

    if (self->cost_map_last)
        occ_map_pixel_map_t_destroy (self->cost_map_last);

    self->cost_map_last = occ_map_pixel_map_t_copy (msg);

    return;

}

static int
find_goal (occ_map::FloatPixelMap *fmap, state_t *self)
{

    // Find the best goal
    int found_goal = 0;
    double max_reward = 0;
    double xy_goal[2] = {0, 0};
    double xyz_car_car[] = {0, 0, 0};
    double xyz_car_local[3];

    
    bot_frames_transform_vec (self->frames, CAR_FRAME, "local", xyz_car_car, xyz_car_local);
    double x_arc, y_arc;
    double angle_deg;
    for (int i=0; i<360; i++) {
        angle_deg = i;
        double xy_arc[2];
        xy_arc[0] = xyz_car_local[0] + self->goal_distance * cos (bot_to_radians (angle_deg));
        xy_arc[1] = xyz_car_local[1] + self->goal_distance * sin (bot_to_radians (angle_deg));

        if (!fmap->isInMap (xy_arc))
            continue;

        double val = fmap->readValue (xy_arc);
        if (val > max_reward) {
            found_goal = 1;
            xy_goal[0] = xy_arc[0];
            xy_goal[1] = xy_arc[1];
            max_reward = val;
        }
    }

    if (found_goal) {
        self->cur_goal[0] = xy_goal[0];
        self->cur_goal[1] = xy_goal[1];
        self->cur_goal[2] = 0;
    }

    return found_goal;
}

static int
find_goal_enhanced (occ_map::FloatPixelMap *fmap, state_t *self)
{
    occ_map::FloatPixelMap *cost_map = new occ_map::FloatPixelMap (fmap);

    //invert the values
    for(int i = 0; i < fmap->dimensions[0]; i++){
        for(int j = 0; j < fmap->dimensions[1]; j++){
            int ixy[2] = {i,j};
            float val = fmax(0.1, fmap->readValue(ixy));
            cost_map->writeValue(ixy, 1/val);
        }
    }
    
    // Find the best goal
    int found_goal = 0;

    double min_cost = 10000;
    double xy_goal[2] = {0, 0};
    double xyz_car_car[] = {0, 0, 0};
    double xyz_car_local[3];

    BotTrans car_to_body; 
    car_to_body.trans_vec[0] = 0;
    car_to_body.trans_vec[1] = -0.3;
    car_to_body.trans_vec[2] = 0;
    
    double rpy[3] = {0};
    bot_roll_pitch_yaw_to_quat(rpy, car_to_body.rot_quat);

    BotTrans body_to_local;

    bot_frames_get_trans(self->frames, "body", "local", 
                         &body_to_local);

    BotTrans car_to_local; 
    bot_trans_apply_trans_to(&body_to_local, &car_to_body, &car_to_local);
    
    xyz_car_local[0] = car_to_local.trans_vec[0];
    xyz_car_local[1] = car_to_local.trans_vec[1];
    
    double x_arc, y_arc;
    double angle_deg;

    double min_dist = fmin(MIN_SCAN_DIST, self->goal_distance);
    double max_dist = self->goal_distance;

    //we should actually ckeck 
    bot_lcmgl_t *lcmgl = self->lcmgl_rays; 
    lcmglColor3f (1.0, 0.0, 0.0);
    bot_lcmgl_line_width(lcmgl, 5);

    int skip = 2;

    for (int i=0; i<360; i+=skip) {
        //get the rays (starting from some distance onwards - to skip too close obstacles 

        angle_deg = i;
        double xy_arc[2];
        xy_arc[0] = xyz_car_local[0] + self->goal_distance * cos (bot_to_radians (angle_deg));
        xy_arc[1] = xyz_car_local[1] + self->goal_distance * sin (bot_to_radians (angle_deg));

        if (!cost_map->isInMap (xy_arc))
            continue;

        double val = cost_map->readValue (xy_arc);
        if (val < min_cost) {
            found_goal = 1;
            xy_goal[0] = xy_arc[0];
            xy_goal[1] = xy_arc[1];
            min_cost = val;
        }
    }

    
    
    int no_beams = 360.0 / skip + 1;

    std::vector<std::pair<int, pos_t> > scores;

    //std::map<int, pos_t> pos_map;

    int count = 0;

    for (int i=0; i<360; i+=skip) {
        count++;
        //get the rays (starting from some distance onwards - to skip too close obstacles 
        angle_deg = i;
        double xy_arc_min[2];
        xy_arc_min[0] = xyz_car_local[0] + min_dist * cos (bot_to_radians (angle_deg));
        xy_arc_min[1] = xyz_car_local[1] + min_dist * sin (bot_to_radians (angle_deg));
      
        double xy_arc_max[2];
        xy_arc_max[0] = xyz_car_local[0] + max_dist * cos (bot_to_radians (angle_deg));
        xy_arc_max[1] = xyz_car_local[1] + max_dist * sin (bot_to_radians (angle_deg));
      
        if (!cost_map->isInMap (xy_arc_min) || !cost_map->isInMap (xy_arc_max))
            continue;
      

        // Find the first point along the ray for which the inverse distance value exceeds VEHICLE_THRESHOLD
        // We will use this to measure the effective length of the ray.
        double collision_point[2];
      
        
        bool estop_collision = cost_map->collisionCheck (xy_arc_min, xy_arc_max, ESTOP_VEHICLE_THRESHOLD, collision_point);
        if (estop_collision)
            continue;


        double threshold = VEHICLE_THRESHOLD;//1/2.0;

        bool collision = cost_map->collisionCheck(xy_arc_min, xy_arc_max, threshold, collision_point); 
      
        double ray_dist = 0;

        //bot_lcmgl_begin(lcmgl, GL_LINES);

        double goal_pos[2];

        if(collision){
            goal_pos[0] = collision_point[0];
            goal_pos[1] = collision_point[1];

            ray_dist = hypot(collision_point[0] -  xyz_car_local[0], collision_point[1] - xyz_car_local[1]);
        }
        else{
            goal_pos[0] = xy_arc_max[0];
            goal_pos[1] = xy_arc_max[1];

            ray_dist = hypot(xy_arc_max[0] -  xyz_car_local[0], xy_arc_max[1] - xyz_car_local[1]);
        }

        //calculate a score 
        //double map_value = cost_map->readValue (goal_pos);
        double map_value = fmax(1/SAFE_DISTANCE, cost_map->readValue (goal_pos));
        double dist_from_goal = hypot(xy_goal[0] - goal_pos[0], xy_goal[1] - goal_pos[1]);
        double heading_delta = fabs(atan2(goal_pos[1], goal_pos[0]) - atan2(xy_goal[1], xy_goal[0]));

        double distance_from_goal_value = ALPHA * pow(dist_from_goal,2);

        double score = pow(ray_dist,DIST_POW) / (1.0 + ALPHA * (heading_delta) / (M_PI/2)) * (1-map_value);  
        pos_t pos;
        pos.xy[0] = goal_pos[0];
        pos.xy[1] = goal_pos[1];
        pos.score = score;//dist_from_goal;

        scores.push_back(std::make_pair<int, pos_t>(i,pos));
    }

    // Perform estop if there are no valid goals
    if (scores.size() == 0) {
        fprintf (stdout, "NO VALID GOAL FOUND\n");
        delete cost_map;
        return 0;
    }

    std::vector<std::pair<int, pos_t> >::iterator it = std::max_element(scores.begin(), scores.end(), score_compare);
    fprintf(stderr, "Max Ind : %d => Score : %f => Pos [%f,%f]\n", it->first, it->second.score, it->second.xy[0], it->second.xy[1]);

    fprintf(stderr, "Goal : %f,%f\n", xy_goal[0], xy_goal[1]);

    fprintf(stderr, "Count  %d No beams : %d\n", count, no_beams);

    double max_score = it->second.score; 

    for(int i=0; i < scores.size(); i++){
        std::pair<int, pos_t> ele = scores[i];
        float *colors = bot_color_util_jet(ele.second.score/ max_score);

        bot_lcmgl_begin(lcmgl, GL_LINES);
        lcmglColor3f (colors[0], colors[1], colors[2]);
        
        bot_lcmgl_vertex3f(lcmgl, xyz_car_local[0], xyz_car_local[1], 0);
        bot_lcmgl_vertex3f(lcmgl, ele.second.xy[0], ele.second.xy[1], 0);

        bot_lcmgl_end(lcmgl);

    }

    bot_lcmgl_switch_buffer(lcmgl);

    if (found_goal) {
        self->cur_goal[0] = it->second.xy[0];
        self->cur_goal[1] = it->second.xy[1];
        self->cur_goal[2] = 0;
    }

    delete cost_map;

    return found_goal;
}

static int
find_goal_enhanced_arc (occ_map::FloatPixelMap *fmap, state_t *self, double *best_steering_angle)
{
    occ_map::FloatPixelMap *cost_map = new occ_map::FloatPixelMap (fmap);

    int draw_arc = 1;

    //invert the values
    for(int i = 0; i < fmap->dimensions[0]; i++){
        for(int j = 0; j < fmap->dimensions[1]; j++){
            int ixy[2] = {i,j};
            float val = fmax(0.1, fmap->readValue(ixy));
            cost_map->writeValue(ixy, 1/val);
        }
    }
    
    // Find the best goal
    int found_goal = 0;
    //double max_reward = 10000;

    double min_cost = 10000;
    double xy_goal[2] = {0, 0};
    double xyz_car_car[] = {0, 0, 0};
    double xyz_car_local[3] = {0};

    double rpy[3] = {0};

    BotTrans car_to_local; 
    bot_frames_get_trans(self->frames, "car", "local", 
                         &car_to_local);

    bot_lcmgl_t *lcmgl = self->lcmgl_arc; 

    xyz_car_local[0] = car_to_local.trans_vec[0];
    xyz_car_local[1] = car_to_local.trans_vec[1];

    /*bot_lcmgl_line_width(lcmgl, 10);
      lcmglColor3f (1.0, 0.0, 0.0);
      lcmglCircle (xyz_car_local, 1.0);

      BotTrans infront_to_car;
      infront_to_car.trans_vec[0] = 15.0;
      infront_to_car.trans_vec[1] = 0;
      infront_to_car.trans_vec[2] = 0;
      bot_roll_pitch_yaw_to_quat(rpy, infront_to_car.rot_quat);

    
      BotTrans infront_to_local; 
      bot_trans_apply_trans_to(&car_to_local, &infront_to_car, &infront_to_local);
    
      double infront_pos[3] = {infront_to_local.trans_vec[0], infront_to_local.trans_vec[1], 0};
      lcmglCircle (infront_pos, 0.5);*/

    //fprintf(stderr, "Car Frame to Local : %f, %f\n", xyz_car_local[0], xyz_car_local[1]);

    //bot_frames_transform_vec (self->frames, CAR_FRAME, "local", xyz_car_car, xyz_car_local);

    //fprintf(stderr, "Car Frame to Local : %f, %f\n", xyz_car_local[0], xyz_car_local[1]);
    
    double x_arc, y_arc;
    double angle_deg;

    double min_dist = fmin(MIN_SCAN_DIST, self->goal_distance);
    double max_dist = self->goal_distance;

    int skip = 2;

    for (int i=0; i<360; i+=skip) {
        //get the rays (starting from some distance onwards - to skip too close obstacles 

        angle_deg = i;
        double xy_arc[2];
        xy_arc[0] = xyz_car_local[0] + self->goal_distance * cos (bot_to_radians (angle_deg));
        xy_arc[1] = xyz_car_local[1] + self->goal_distance * sin (bot_to_radians (angle_deg));
 
        if (!cost_map->isInMap (xy_arc))
            continue;

        double val = cost_map->readValue (xy_arc);
        if (val < min_cost) {
            found_goal = 1;
            xy_goal[0] = xy_arc[0];
            xy_goal[1] = xy_arc[1];
            min_cost = val;
        }
    }

    if(found_goal == 0){
        fprintf(stderr, "No goal found\n");
        delete cost_map;        
        return found_goal;
    }

    //we should actually ckeck 

    
    double xyz_goal_local[3] = {xy_goal[0], xy_goal[1], 0};
    lcmglColor3f (1.0, 0.0, 1.0);
    lcmglCircle (xyz_goal_local, 0.6);

    
    lcmglColor3f (1.0, 0.0, 0.0);
    bot_lcmgl_line_width(lcmgl, 5);

    int arc_size = self->goal_distance;
    double max_steering_angle = bot_to_radians(90);
    double delta = max_steering_angle / arc_size;

    double l = 1.8;
    double w = 1.2; 
    
    lcmglColor3f (1.0,0.0,0.0);
    
    std::vector<steering_goal_t> scores;

    for(int j=-arc_size; j <= arc_size; j++){
        double angle = delta * j * STEERING_RATIO;
        double arc_point[2] = {0,0};

        if(fabs(angle) > 0.001){
            double rad = pow( pow(l/ tan(angle),2) + pow(l,2), 0.5);
            double swept_angle = self->goal_distance / rad;

            double start_angle = MIN_SCAN_DIST / rad;
            
            int no_segments = 20;

            double angle_d = (swept_angle - start_angle)/ no_segments;
            
            double s_angle = start_angle;
            
            double start_s, start_c;
            bot_fasttrig_sincos(start_angle, &start_s, &start_c);

            double last_xy[2] = {rad * start_s, rad*(1-start_c)};
            if(angle < 0){
                last_xy[1] = - last_xy[1];
            }
            
            bool estop_collision = false;
            double arc_length = rad * s_angle;
            
            steering_goal_t c_goal;
            
            for(int i=1; i < no_segments; i++){
                double theta = angle_d * i + s_angle;
                double s, c;
                bot_fasttrig_sincos(theta, &s, &c);
                double x = rad * s;                              
                double theta_act = theta;
                double y = rad*(1-c);

                if(angle < 0){
                    y = -y;                    
                }

                BotTrans new_car_s_to_car;
                new_car_s_to_car.trans_vec[0] = last_xy[0];
                new_car_s_to_car.trans_vec[1] = last_xy[1];
                new_car_s_to_car.trans_vec[2] = 0;
                bot_roll_pitch_yaw_to_quat(rpy, new_car_s_to_car.rot_quat);

                BotTrans new_car_e_to_car;
                new_car_e_to_car.trans_vec[0] = x;
                new_car_e_to_car.trans_vec[1] = y;
                new_car_e_to_car.trans_vec[2] = 0;
                bot_roll_pitch_yaw_to_quat(rpy, new_car_e_to_car.rot_quat);
                
                
                BotTrans car_s_to_local; 
                bot_trans_apply_trans_to(&car_to_local, &new_car_s_to_car, &car_s_to_local);

                BotTrans car_e_to_local; 
                bot_trans_apply_trans_to(&car_to_local, &new_car_e_to_car, &car_e_to_local);

                double car_start[2] = {car_s_to_local.trans_vec[0], car_s_to_local.trans_vec[1]};
                double car_stop[2] = {car_e_to_local.trans_vec[0], car_e_to_local.trans_vec[1]};
                
                double collision_point[2];
                 
                estop_collision = cost_map->collisionCheck (car_start, car_stop, VEHICLE_THRESHOLD_ARC, collision_point);

                if (!cost_map->isInMap (car_stop)){
                    break;
                }

                if(estop_collision){
                    break;
                }

                if(i==1 && draw_arc){
                    xy_t sp;
                    sp.xy[0] = car_start[0];
                    sp.xy[1] = car_start[1];
                    c_goal.sample_points.push_back(sp);
                }

                last_xy[0] = x;
                last_xy[1] = y;
                arc_length = rad * theta;
                arc_point[0] = car_stop[0];
                arc_point[1] = car_stop[1];
                if(draw_arc){
                    xy_t ap;
                    ap.xy[0] = car_stop[0];
                    ap.xy[1] = car_stop[1];
                    c_goal.sample_points.push_back(ap);
                }
            }

            c_goal.pos.xy[0] = arc_point[0];
            c_goal.pos.xy[1] = arc_point[1];

            double map_value = fmax(1/SAFE_DISTANCE, cost_map->readValue (arc_point));

            double dist_from_goal = hypot(xy_goal[0] - arc_point[0], xy_goal[1] - arc_point[1]);

            double heading_delta = fabs(atan2(arc_point[1], arc_point[0]) - atan2(xy_goal[1], xy_goal[0]));

            double distance_from_goal_value = ALPHA * pow(dist_from_goal,2);

            double score = pow(arc_length,DIST_POW) / (1.0 + ALPHA * (heading_delta) / (M_PI/2)) * (1-map_value);  


            c_goal.pos.score = score;
            c_goal.steering_angle = delta * j;

            scores.push_back(c_goal);

            if(!estop_collision){
            }
        }
    }

    //do one for stright 
    //this should evaluvate the straight line path 
    bool estop_collision = false;

    BotTrans new_car_s_to_car;
    new_car_s_to_car.trans_vec[0] = min_dist;
    new_car_s_to_car.trans_vec[1] = 0;
    new_car_s_to_car.trans_vec[2] = 0;
    bot_roll_pitch_yaw_to_quat(rpy, new_car_s_to_car.rot_quat);

    BotTrans new_car_e_to_car;
    new_car_e_to_car.trans_vec[0] = max_dist;
    new_car_e_to_car.trans_vec[1] = 0;
    new_car_e_to_car.trans_vec[2] = 0;
    bot_roll_pitch_yaw_to_quat(rpy, new_car_e_to_car.rot_quat);

    BotTrans car_s_to_local; 
    bot_trans_apply_trans_to(&car_to_local, &new_car_s_to_car, &car_s_to_local);

    BotTrans car_e_to_local; 
    bot_trans_apply_trans_to(&car_to_local, &new_car_e_to_car, &car_e_to_local);

    double car_start[2] = {car_s_to_local.trans_vec[0], car_s_to_local.trans_vec[1]};
    double car_stop[2] = {car_e_to_local.trans_vec[0], car_e_to_local.trans_vec[1]};
                
    double arc_length = min_dist;

    double collision_point[2];
                 
    estop_collision = cost_map->collisionCheck (car_start, car_stop, VEHICLE_THRESHOLD, collision_point);

    if (cost_map->isInMap (car_stop)){
        steering_goal_t c_goal;
        
        if(draw_arc){
            xy_t sp;
            sp.xy[0] = car_start[0];
            sp.xy[1] = car_start[1];
            c_goal.sample_points.push_back(sp);
        }

        if(estop_collision){
            arc_length = hypot(collision_point[0] - xyz_car_local[0], collision_point[1] - xyz_car_local[1]);

            c_goal.pos.xy[0] = collision_point[0];
            c_goal.pos.xy[1] = collision_point[1];

            if(draw_arc){
                xy_t ap;
                ap.xy[0] = collision_point[0];
                ap.xy[1] = collision_point[1];
                c_goal.sample_points.push_back(ap);
            }

            double map_value = fmax(1/SAFE_DISTANCE, cost_map->readValue (collision_point));

            double dist_from_goal = hypot(xy_goal[0] - collision_point[0], xy_goal[1] - collision_point[1]);

            double heading_delta = fabs(atan2(collision_point[1], collision_point[0]) - atan2(xy_goal[1], xy_goal[0]));

            double distance_from_goal_value = ALPHA * pow(dist_from_goal,2);

            double score = pow(arc_length,DIST_POW) / (1.0 + ALPHA * (heading_delta) / (M_PI/2)) * (1-map_value);  

            c_goal.pos.score = score;

            c_goal.steering_angle = 0;

            scores.push_back(c_goal);
        }
        else{
            arc_length = hypot(car_stop[0] - xyz_car_local[0], car_stop[1] - xyz_car_local[1]);
            c_goal.pos.xy[0] = car_stop[0];
            c_goal.pos.xy[1] = car_stop[1];
            
            if(draw_arc){
                xy_t ap;
                ap.xy[0] = car_stop[0];
                ap.xy[1] = car_stop[1];
                c_goal.sample_points.push_back(ap);
            }

            double map_value = fmax(1/SAFE_DISTANCE, cost_map->readValue (car_stop));

            double dist_from_goal = hypot(xy_goal[0] - car_stop[0], xy_goal[1] - car_stop[1]);

            double heading_delta = fabs(atan2(car_stop[1], car_stop[0]) - atan2(xy_goal[1], xy_goal[0]));

            double distance_from_goal_value = ALPHA * pow(dist_from_goal,2);

            double score = pow(arc_length,DIST_POW) / (1.0 + ALPHA * (heading_delta) / (M_PI/2)) * (1-map_value);  

            c_goal.pos.score = score;
            c_goal.steering_angle = 0;

            scores.push_back(c_goal);
        }
    }

    double max_score = 0;

    std::vector<steering_goal_t>::iterator it = std::max_element(scores.begin(), scores.end(), steering_score_compare);
    fprintf(stderr, "Steering angle : %f => Score : %f => Pos [%f,%f]\n", bot_to_degrees(it->steering_angle), it->pos.score, it->pos.xy[0], it->pos.xy[1]);

    max_score = it->pos.score;

    *best_steering_angle = it->steering_angle;
    
    fprintf(stderr, "Goal : %f,%f\n", xy_goal[0], xy_goal[1]);

    //draw the arcs
    if(draw_arc){
        for(int i=0; i < scores.size(); i++){
            steering_goal_t sg = scores[i];
            
            float *colors = bot_color_util_jet( sg.pos.score / max_score);
            lcmglColor3f (colors[0], colors[1], colors[2]);
            
            bot_lcmgl_line_width(lcmgl, 5);
            
            if(sg.sample_points.size() < 2)
                continue;
            bot_lcmgl_begin(lcmgl, GL_LINES);
            for(int j=1; j < sg.sample_points.size(); j++){
                bot_lcmgl_vertex3f(lcmgl, sg.sample_points[j-1].xy[0], sg.sample_points[j-1].xy[1], 0);
                bot_lcmgl_vertex3f(lcmgl, sg.sample_points[j].xy[0], sg.sample_points[j].xy[1], 0);
            }
            bot_lcmgl_end(lcmgl);
        }        
        bot_lcmgl_switch_buffer(lcmgl);
    }

    if (found_goal) {
        self->cur_goal[0] = it->pos.xy[0];
        self->cur_goal[1] = it->pos.xy[1];
        self->cur_goal[2] = 0;
    }
    
    delete cost_map;

    return found_goal;
}

static int
find_goal_enhanced_with_tld_heading (occ_map::FloatPixelMap *fmap, state_t *self)
{
    if(self->tld_bearing == NULL){
        fprintf(stderr, "No TLD bearing found\n");
        return -1;
    }
    
    //we assume that this is the same for the car frame - because this is in the body frame 
    double long_goal_heading = atan2(self->tld_bearing->vec[1], self->tld_bearing->vec[0]);
    
    fprintf(stderr, "Longterm TLD heading : %f\n", bot_to_radians(long_goal_heading));

    occ_map::FloatPixelMap *cost_map = new occ_map::FloatPixelMap (fmap);

    //invert the values
    for(int i = 0; i < fmap->dimensions[0]; i++){
        for(int j = 0; j < fmap->dimensions[1]; j++){
            int ixy[2] = {i,j};
            float val = fmax(0.1, fmap->readValue(ixy));
            cost_map->writeValue(ixy, 1/val);
        }
    }
    
    // Find the best goal
    int found_goal = 1;
    //double max_reward = 10000;

    double min_cost = 10000;
    double xy_goal[2] = {0, 0};
    double xyz_car_car[] = {0, 0, 0};
    double xyz_car_local[3];

    BotTrans car_to_body; 
    car_to_body.trans_vec[0] = 0;
    car_to_body.trans_vec[1] = -0.3;
    car_to_body.trans_vec[2] = 0;
    
    double rpy[3] = {0};
    bot_roll_pitch_yaw_to_quat(rpy, car_to_body.rot_quat);

    BotTrans body_to_local;

    bot_frames_get_trans(self->frames, "body", "local", 
                         &body_to_local);


    BotTrans long_goal_to_car;
    long_goal_to_car.trans_vec[0] = self->goal_distance * cos(long_goal_heading);
    long_goal_to_car.trans_vec[1] = self->goal_distance * sin(long_goal_heading);
    long_goal_to_car.trans_vec[2] = 0;
    
    bot_roll_pitch_yaw_to_quat(rpy, long_goal_to_car.rot_quat);
    
    BotTrans car_to_local; 
    bot_trans_apply_trans_to(&body_to_local, &car_to_body, &car_to_local);
    
    xyz_car_local[0] = car_to_local.trans_vec[0];
    xyz_car_local[1] = car_to_local.trans_vec[1];

    //fprintf(stderr, "Car Frame to Local : %f, %f\n", xyz_car_local[0], xyz_car_local[1]);

    //bot_frames_transform_vec (self->frames, CAR_FRAME, "local", xyz_car_car, xyz_car_local);

    //fprintf(stderr, "Car Frame to Local : %f, %f\n", xyz_car_local[0], xyz_car_local[1]);
    
    double x_arc, y_arc;
    double angle_deg;

    double min_dist = fmin(MIN_SCAN_DIST, self->goal_distance);
    double max_dist = self->goal_distance;

    //we should actually ckeck 
    bot_lcmgl_t *lcmgl = self->lcmgl_rays; 
    lcmglColor3f (1.0, 0.0, 0.0);
    bot_lcmgl_line_width(lcmgl, 5);

    int skip = 2;
    

    BotTrans long_goal_to_local; 
    bot_trans_apply_trans_to(&car_to_local, &long_goal_to_car, &long_goal_to_local);
    
    xy_goal[0] = long_goal_to_local.trans_vec[0], xy_goal[1] = long_goal_to_local.trans_vec[1];

    fprintf(stderr, "Goal Position (local) : %f,%f\n", xy_goal[0], xy_goal[1]);
        
    int no_beams = 360.0 / skip + 1;

    std::vector<std::pair<int, pos_t> > scores;

    //std::map<int, pos_t> pos_map;

    int count = 0;

    
    for (int i=0; i<360; i+=skip) {
        count++;
        //get the rays (starting from some distance onwards - to skip too close obstacles 
        angle_deg = i;
        double xy_arc_min[2];
        xy_arc_min[0] = xyz_car_local[0] + min_dist * cos (bot_to_radians (angle_deg));
        xy_arc_min[1] = xyz_car_local[1] + min_dist * sin (bot_to_radians (angle_deg));
      
        double xy_arc_max[2];
        xy_arc_max[0] = xyz_car_local[0] + max_dist * cos (bot_to_radians (angle_deg));
        xy_arc_max[1] = xyz_car_local[1] + max_dist * sin (bot_to_radians (angle_deg));
      
        if (!cost_map->isInMap (xy_arc_min) || !cost_map->isInMap (xy_arc_max))
            continue;
      
        double collision_point[2];
      
        double threshold = VEHICLE_THRESHOLD;//1/2.0;

        bool collision = cost_map->collisionCheck(xy_arc_min, xy_arc_max, threshold, collision_point); 
      
        double ray_dist = 0;

        

        double goal_pos[2];

        

        if(collision){
            goal_pos[0] = collision_point[0];
            goal_pos[1] = collision_point[1];

            
            ray_dist = hypot(collision_point[0] -  xyz_car_local[0], collision_point[1] - xyz_car_local[1]);
        }
        else{
            goal_pos[0] = xy_arc_max[0];
            goal_pos[1] = xy_arc_max[1];

           
            ray_dist = hypot(xy_arc_max[0] -  xyz_car_local[0], xy_arc_max[1] - xyz_car_local[1]);
        }

        double map_value = fmax(1/SAFE_DISTANCE, cost_map->readValue (goal_pos));
        //calculate a score 
        
        double dist_from_goal = hypot(xy_goal[0] - goal_pos[0], xy_goal[1] - goal_pos[1]);

        //we should calculate a better score 
        //double score = dist_from_goal;

        double heading_delta = fabs(atan2(goal_pos[1], goal_pos[0]) - atan2(xy_goal[1], xy_goal[0]));

        double distance_from_goal_value = ALPHA * pow(dist_from_goal,2);

        double score = pow(ray_dist,DIST_POW) / (1.0 + ALPHA * (heading_delta) / (M_PI/2)) * (1-map_value);  

        //fprintf(stderr, "Ray : %f Distance from Goal : %f, Heading Delta : %f- Score : %f, Map Value : %f\n", ray_dist, dist_from_goal, heading_delta, score, map_value); 

        float *colors = bot_color_util_jet(fmin(pow(score,DIST_POW),self->goal_distance)/self->goal_distance);

        //bot_lcmgl_begin(lcmgl, GL_LINES);
        //lcmglColor3f (colors[0], colors[1], colors[2]);
        
        /*if(collision){
          bot_lcmgl_vertex3f(lcmgl, xyz_car_local[0], xyz_car_local[1], 0);
          bot_lcmgl_vertex3f(lcmgl, collision_point[0], collision_point[1], 0);
          }
          else{
          bot_lcmgl_vertex3f(lcmgl, xyz_car_local[0], xyz_car_local[1], 0);
          bot_lcmgl_vertex3f(lcmgl, xy_arc_max[0], xy_arc_max[1], 0);
          }*/
        //scores.push_back(std::make_pair<int, double>(i,dist_from_goal));
        pos_t pos;
        pos.xy[0] = goal_pos[0];
        pos.xy[1] = goal_pos[1];
        pos.score = score;
        scores.push_back(std::make_pair<int, pos_t>(i,pos));

        //bot_lcmgl_end(lcmgl);

        //score each ray - to find the best one 
             
      
        //fprintf(stderr, "[%d] Dist : %f\n", i, ray_dist); 
    }

    //std::vector<std::pair<int, double> >::iterator it = std::max_element(scores.begin(), scores.end(), score_compare);
    std::vector<std::pair<int, pos_t> >::iterator it = std::max_element(scores.begin(), scores.end(), score_compare);

    double max_score = it->second.score; 

    for(int i=0; i < scores.size(); i++){
        std::pair<int, pos_t> ele = scores[i];
        float *colors = bot_color_util_jet(ele.second.score/ max_score);

        bot_lcmgl_begin(lcmgl, GL_LINES);
        lcmglColor3f (colors[0], colors[1], colors[2]);
        
        bot_lcmgl_vertex3f(lcmgl, xyz_car_local[0], xyz_car_local[1], 0);
        bot_lcmgl_vertex3f(lcmgl, ele.second.xy[0], ele.second.xy[1], 0);

        bot_lcmgl_end(lcmgl);

    }

    
    fprintf(stderr, "Max Ind : %d => Score : %f => Pos [%f,%f]\n", it->first, it->second.score, it->second.xy[0], it->second.xy[1]);

    fprintf(stderr, "Goal : %f,%f\n", xy_goal[0], xy_goal[1]);

    fprintf(stderr, "Count  %d No beams : %d\n", count, no_beams);

    bot_lcmgl_switch_buffer(lcmgl);

    /**/

    if (found_goal) {
        /*self->cur_goal[0] = xy_goal[0];
          self->cur_goal[1] = xy_goal[1];
          self->cur_goal[2] = 0;*/
        self->cur_goal[0] = it->second.xy[0];
        self->cur_goal[1] = it->second.xy[1];
        self->cur_goal[2] = 0;
    }

    delete cost_map;

    return found_goal;
}

static int
find_goal_with_only_tld_heading (state_t *self)
{
    if(self->tld_bearing == NULL){
        fprintf(stderr, "No TLD bearing found\n");
        return -1;
    }
    
    //we assume that this is the same for the car frame - because this is in the body frame 
    double long_goal_heading = atan2(self->tld_bearing->vec[1], self->tld_bearing->vec[0]);
    
    fprintf(stderr, "Longterm TLD heading : %f\n", bot_to_radians(long_goal_heading));

    // Find the best goal
    int found_goal = 1;
    //double max_reward = 10000;

    double min_cost = 10000;
    double xy_goal[2] = {0, 0};
    double xyz_car_car[] = {0, 0, 0};
    double xyz_car_local[3];

    BotTrans car_to_body; 
    car_to_body.trans_vec[0] = 0;
    car_to_body.trans_vec[1] = -0.3;
    car_to_body.trans_vec[2] = 0;
    
    double rpy[3] = {0};
    bot_roll_pitch_yaw_to_quat(rpy, car_to_body.rot_quat);

    BotTrans body_to_local;

    bot_frames_get_trans(self->frames, "body", "local", 
                         &body_to_local);


    BotTrans long_goal_to_car;
    long_goal_to_car.trans_vec[0] = self->goal_distance * cos(long_goal_heading);
    long_goal_to_car.trans_vec[1] = self->goal_distance * sin(long_goal_heading);
    long_goal_to_car.trans_vec[2] = 0;
    
    bot_roll_pitch_yaw_to_quat(rpy, long_goal_to_car.rot_quat);
    
    BotTrans car_to_local; 
    bot_trans_apply_trans_to(&body_to_local, &car_to_body, &car_to_local);
    
    xyz_car_local[0] = car_to_local.trans_vec[0];
    xyz_car_local[1] = car_to_local.trans_vec[1];

    double x_arc, y_arc;
    double angle_deg;

    double min_dist = fmin(MIN_SCAN_DIST, self->goal_distance);
    double max_dist = self->goal_distance;

    //we should actually ckeck 
    bot_lcmgl_t *lcmgl = self->lcmgl_rays; 
    lcmglColor3f (1.0, 0.0, 0.0);
    bot_lcmgl_line_width(lcmgl, 5);

    BotTrans long_goal_to_local; 
    bot_trans_apply_trans_to(&car_to_local, &long_goal_to_car, &long_goal_to_local);
    
    xy_goal[0] = long_goal_to_local.trans_vec[0], xy_goal[1] = long_goal_to_local.trans_vec[1];

    fprintf(stderr, "Goal Position (local) : %f,%f\n", xy_goal[0], xy_goal[1]);
        
    if (found_goal) {
        self->cur_goal[0] = xy_goal[0];
        self->cur_goal[1] = xy_goal[1];
        self->cur_goal[2] = 0;
    }
    return found_goal;
}

static void
perform_emergency_stop (state_t *self)
{
    self->throttle_val = 0;
    if(self->do_braking){
        self->brake_val = 1.0;
    }
    else{
        self->brake_val = 0;
    }
    if(self->brake_val > 0){
        fprintf (stdout, "Performing emergency stop - brake : %f!!!!\n", self->brake_val);
    }
    drc_driving_control_cmd_t msg;
    msg.utime = bot_timestamp_now();
    msg.type = DRC_DRIVING_CONTROL_CMD_T_TYPE_BRAKE; 
    msg.brake_value = self->brake_val;
    msg.steering_angle = 0;
    msg.throttle_value = self->throttle_val;
    drc_driving_control_cmd_t_publish(self->lcm, "DRC_DRIVING_COMMAND", &msg);
}

static gboolean
status_timer (gpointer data)
{
    state_t *self = (state_t *) data;
    publish_system_status(self);    
    return TRUE;
}

static gboolean
publish_state_timer (gpointer data)
{
    state_t *self = (state_t *) data;
    publish_system_state_values(self);    
    return TRUE;
}

static gboolean
on_controller_timer (gpointer data)
{

    state_t *self = (state_t *) data;

    if((self->utime - self->last_controller_utime) / 1.0e3 < self->timer_period){
        if(self->verbose)
            fprintf(stderr, "Timer gap : %f (ms)\n", (self->utime - self->last_controller_utime) / 1.0e3);
        return TRUE;
    }

    self->last_controller_utime = self->utime;

    // Perform estop if driving duration has expired
    if(self->drive_duration < 0){
        self->curr_state = IDLE;
        perform_emergency_stop(self);
        publish_status(self);
        return TRUE;
    }

    //drive duration is over - we need to stop 
    if((self->utime - self->drive_start_time)/1.0e6 > self->drive_duration){
        //drc_driving_control_cmd_t msg;
        //msg.utime = bot_timestamp_now();
        perform_emergency_stop(self);
        self->drive_duration = -1;
        self->curr_state = IDLE;
        publish_status(self);
        return TRUE;
    }

    
    // Perform emergency stop if there isn't a sufficient amount of terrain
    // classified as road in front of the vehicle

    int turn_only = 0;
    if((self->utime - self->drive_start_time)/1.0e6 < TIME_TO_TURN){
        turn_only = 1;
    }

    int use_road_carrot = 1;
    int use_road_arc = 0;
    int use_tld = 0;
    int user_goal = 0;
    if(self->last_driving_cmd){
        if(self->last_driving_cmd->type == DRC_DRIVING_CMD_T_TYPE_USE_ROAD_LOOKAHEAD_CARROT_B || self->last_driving_cmd->type == DRC_DRIVING_CMD_T_TYPE_USE_ROAD_LOOKAHEAD_CARROT_NB){
            use_road_carrot = 1;
        }
        else if (self->last_driving_cmd->type == DRC_DRIVING_CMD_T_TYPE_USE_ROAD_LOOKAHEAD_ARC_B || self->last_driving_cmd->type == DRC_DRIVING_CMD_T_TYPE_USE_ROAD_LOOKAHEAD_ARC_NB) {
            use_road_carrot = 0;
            use_road_arc = 1;
        }
        else if(self->last_driving_cmd->type == DRC_DRIVING_CMD_T_TYPE_USE_TLD_LOOKAHEAD_WITH_ROAD_B || self->last_driving_cmd->type == DRC_DRIVING_CMD_T_TYPE_USE_TLD_LOOKAHEAD_WITH_ROAD_NB){
            use_road_carrot = 1;
            use_tld = 1;
        }
        else if(self->last_driving_cmd->type == DRC_DRIVING_CMD_T_TYPE_USE_TLD_LOOKAHEAD_IGNORE_ROAD_B || self->last_driving_cmd->type == DRC_DRIVING_CMD_T_TYPE_USE_TLD_LOOKAHEAD_IGNORE_ROAD_NB){
            use_road_carrot = 0;
            use_road_arc = 0;
            use_tld = 1;
        }
        else if(self->last_driving_cmd->type == DRC_DRIVING_CMD_T_TYPE_USE_USER_HEADING_B || self->last_driving_cmd->type == DRC_DRIVING_CMD_T_TYPE_USE_USER_HEADING_NB){
            use_road_carrot = 0;
            use_road_arc = 0;
            use_tld =0;
            user_goal = 1;
        }
    }

    // If we are in autonomous driving mode, perform e-stop if we don't have
    // a terrain map or the map that we have is too old.
    if(use_road_arc || use_road_carrot){
        if (!self->cost_map_last) {
            //if (self->verbose)
            fprintf (stdout, "No valid terrain classification map\n");
            self->curr_state = ERROR_NO_MAP;
            perform_emergency_stop(self);
            publish_status(self);
            return TRUE;
        }
        else {
            //we should check if the map is stale - because of some issue with the controller   
            if((self->utime - self->cost_map_last->utime)/1.0e6 > TIMEOUT_THRESHOLD){
                fprintf(stderr, "Error - Costmap too old - asking for EStop\n");
                self->curr_state = ERROR_MAP_TIMEOUT;
                perform_emergency_stop(self);
                publish_status(self);
                return TRUE;
            }
        }
    }
    
    if(use_tld && fabs(self->utime - self->tld_bearing->utime)/1.0e6 > TLD_TIMEOUT_SEC){
        fprintf(stderr, "Error : Asked to follow TLD - but no TLD heading\n");
        self->curr_state = ERROR_TLD_TIMEOUT;
        publish_status(self);
        return TRUE;        
    }

    //add code to handle the TLD heading if we have it 

    static int64_t last_utime = 0;
    
    fprintf(stdout, "Drive start time : %f\n", (self->utime - self->drive_start_time)/1.0e6);

    self->drive_time_to_go = self->drive_duration*1.0e6 - (self->utime - self->drive_start_time);

    //draw_goal_range (self);    
    double xyz_goal[3];

    double steering_angle_arc = 0;

    if((use_road_carrot || use_road_arc) && !use_tld){
        occ_map::FloatPixelMap *fmap = new occ_map::FloatPixelMap (self->cost_map_last);
        int have_valid_goal = 0;
        //fprintf (stdout, "use_road_arc = %d\n");
        if(use_road_arc) {
            
            if (find_goal_enhanced_arc (fmap, self, &steering_angle_arc)) {
                have_valid_goal = 1;
                self->have_valid_goal = 1;
                self->curr_state = DRIVING_ROAD_ONLY_ARC;
            }
        }
        else if (use_road_carrot) {
            if (find_goal_enhanced (fmap, self)) {
                have_valid_goal = 1;
                draw_goal (self);
                self->have_valid_goal = 1;
                self->curr_state = DRIVING_ROAD_ONLY_CARROT;
            }
        }

        if (!have_valid_goal) {
            if (self->verbose)
                fprintf (stdout, "Unable to find goal in PixMap - Stopping\n");
            
            perform_emergency_stop(self);
            self->drive_duration = -1;
            self->curr_state = ERROR_NO_VALID_GOAL;
            publish_status(self);
            delete fmap;
            return TRUE;
        }
    }
    else if(use_road_carrot && use_tld){
        occ_map::FloatPixelMap *fmap = new occ_map::FloatPixelMap (self->cost_map_last);
        if (find_goal_enhanced_with_tld_heading (fmap, self)) {
            draw_goal (self);
            self->have_valid_goal = 1;
            self->curr_state = DRIVING_TLD_AND_ROAD;
        }
        else {
            if (self->verbose)
                fprintf (stdout, "Unable to find goal in PixMap using TLD heading - Stopping\n");
            
            perform_emergency_stop(self);
            self->drive_duration = -1;
            self->curr_state = ERROR_NO_VALID_GOAL;
            publish_status(self);
            delete fmap;
            return TRUE;
        }
    }
    else if(use_tld && !use_road_carrot){
        //fprintf(stderr, "Not handled right now - returning\n");
        if (find_goal_with_only_tld_heading(self)){
            draw_goal (self);
            self->have_valid_goal = 1;
            self->curr_state = DRIVING_TLD;
        }
        else{
            if (self->verbose)
                fprintf (stdout, "Unable to find goal in PixMap using TLD heading - Stopping\n");
            self->curr_state = ERROR_NO_VALID_GOAL;
            perform_emergency_stop(self);
            self->drive_duration = -1;
            publish_status(self);
            return TRUE;

        }
    }
    else if(user_goal){
        double user_steering_angle = bot_to_radians(self->last_driving_cmd->steering_angle_degrees);
        double throttle_val = 0;
        double brake_val = 0;
        
        if(turn_only){
            throttle_val = 0;
            self->curr_state = DOING_INITIAL_TURN;
	    brake_val = 0;
	    if(self->do_braking){
                brake_val = 1.0;
	    }
	    
        }
        else if((self->utime - self->drive_start_time)/1.0e6 < (self->throttle_duration + TIME_TO_TURN)){
            throttle_val = self->throttle_ratio;
        }        

	if(self->do_braking){
            if((self->utime - self->drive_start_time)/1.0e6 > (self->drive_duration - TIME_TO_BRAKE)){
                double brake_time = (self->utime - self->drive_start_time)/1.0e6 - (self->drive_duration - TIME_TO_BRAKE);
                throttle_val = 0;
                self->curr_state = DOING_BRAKING;
                if(TIME_TO_BRAKE > 0)
                    brake_val = MAX_BRAKE * fmax(0, fmin(1, brake_time / TIME_TO_BRAKE));
                else
                    brake_val = MAX_BRAKE;
	    
                //time to brake - gracefully
            }
	}	

        double max_angle = 90;
        double steering_input = fmax(bot_to_radians(-max_angle), fmin(bot_to_radians(max_angle), user_steering_angle));

        fprintf (stdout, "Steering control: Signal = %.2f (deg) Throttle : %f Brake: %f\n",
                 bot_to_degrees(steering_input), 
                 throttle_val, brake_val);

        if(self->time_applied_to_brake > 0 && self->time_applied_to_accelerate)
            fprintf(stderr, "Time accelerating : %f, Time braking : %f\n", self->time_applied_to_accelerate / 1.0e6, 
                    self->time_applied_to_brake / 1.0e6);

        drc_driving_control_cmd_t msg;
        msg.utime = bot_timestamp_now();
        msg.type = DRC_DRIVING_CONTROL_CMD_T_TYPE_DRIVE;
        msg.steering_angle =  steering_input;

        msg.throttle_value = throttle_val;
        msg.brake_value = brake_val;
        drc_driving_control_cmd_t_publish(self->lcm, "DRC_DRIVING_COMMAND", &msg);

        self->throttle_val = throttle_val;
        self->brake_val = brake_val;
        self->hand_steer = steering_input;  

        publish_status(self);

        return TRUE;
        
    }
    else{
        fprintf(stderr, "Error: Unknown drive command type - skipping\n");
        perform_emergency_stop(self);
        self->curr_state = ERROR_NO_VALID_GOAL;
        self->drive_duration = -1;
        publish_status(self);

        last_utime = self->utime;

        return TRUE;
    }
    

    if (self->have_valid_goal) {
        double steering_input = 0;
        // Compute the steering command
        if(use_road_arc){
            steering_input = steering_angle_arc;
        }
        else{
            double xyz_goal_car[3];
            bot_frames_transform_vec (self->frames, "local", "body", self->cur_goal, xyz_goal_car);

            BotTrans goal_to_body;
            goal_to_body.trans_vec[0] = xyz_goal_car[0];
            goal_to_body.trans_vec[1] = xyz_goal_car[1];
            goal_to_body.trans_vec[2] = 0;

            double rpy[3] = {0};
            bot_roll_pitch_yaw_to_quat(rpy, goal_to_body.rot_quat);
	
            BotTrans body_to_car; 
            body_to_car.trans_vec[0] = 0;
            body_to_car.trans_vec[1] = 0.3;
            body_to_car.trans_vec[2] = 0;

            bot_roll_pitch_yaw_to_quat(rpy, body_to_car.rot_quat);
	
            BotTrans goal_to_car; 
            bot_trans_apply_trans_to(&body_to_car, &goal_to_body, &goal_to_car);

            double heading_error = bot_fasttrig_atan2 (goal_to_car.trans_vec[1], goal_to_car.trans_vec[0]);  // xyz_goal_car[1], xyz_goal_car[0]);

            self->goal_heading = heading_error;
            self->actual_goal_distance = hypot(goal_to_car.trans_vec[1], goal_to_car.trans_vec[0]);

            fprintf(stderr, "Gains => p : %f d : %f \n\tComponents => p_c : %f d_c : %f\n", self->kp_steer, self->kd_steer, 
                    self->kp_steer * heading_error, self->kd_steer * (heading_error - self->error_last));


            steering_input = self->kp_steer * heading_error + self->kd_steer * (heading_error - self->error_last);
        
            self->error_last = heading_error;

            if(self->use_differential_angle){
                steering_input = bot_clamp(steering_input, -bot_to_radians(10), bot_to_radians(10));
            }

            double steering_angle_delta = steering_input;
	
            //angle at which accceleration should be down - this might be tough when actually controlling the car 
            //using the robot 
            double steering_no_angle;
            if(self->use_differential_angle){
                if(steering_angle_delta >= 0){
                    steering_no_angle = bot_to_radians(4);
                }
                else{
                    steering_no_angle = bot_to_radians(-4);
                }
            }
        }
        //double throttle_ratio = (steering_no_angle - steering_angle_delta) / steering_no_angle;
        //throttle_ratio = fmin(1.0, fmax(throttle_ratio, -2.0));
        double throttle_val = 0;
        double brake_val = 0;
        
        if(turn_only){
            throttle_val = 0;
            self->curr_state = DOING_INITIAL_TURN;
	    brake_val = 0;
	    if(self->do_braking){
                brake_val = 1.0;
	    }
        }
        else if((self->utime - self->drive_start_time)/1.0e6 < (self->throttle_duration + TIME_TO_TURN)){
            throttle_val = self->throttle_ratio;            
        }        
        
	if(self->do_braking){
            if((self->utime - self->drive_start_time)/1.0e6 > (self->drive_duration - TIME_TO_BRAKE)){
                double brake_time = (self->utime - self->drive_start_time)/1.0e6 - (self->drive_duration - TIME_TO_BRAKE);
                throttle_val = 0;
                self->curr_state = DOING_BRAKING;
                if(TIME_TO_BRAKE > 0)
                    brake_val = MAX_BRAKE * fmax(0, fmin(1, brake_time / TIME_TO_BRAKE));
                else
                    brake_val = MAX_BRAKE;
            
                //time to brake - gracefully
            }
	}	
        
        double max_angle = 90;
        steering_input = fmax(bot_to_radians(-max_angle), fmin(bot_to_radians(max_angle), steering_input));

        /*fprintf (stdout, "Steering control: Error = %.2f deg, Signal = %.2f (deg) Throttle : %f Brake: %f\n",
          bot_to_degrees(heading_error), bot_to_degrees(steering_input), 
          throttle_val, brake_val);*/

        if(self->time_applied_to_brake > 0 && self->time_applied_to_accelerate)
            fprintf(stderr, "Time accelerating : %f, Time braking : %f\n", self->time_applied_to_accelerate / 1.0e6, 
                    self->time_applied_to_brake / 1.0e6);

        drc_driving_control_cmd_t msg;
        msg.utime = bot_timestamp_now();
        msg.type = DRC_DRIVING_CONTROL_CMD_T_TYPE_DRIVE; 
        msg.steering_angle =  steering_input;
    
        msg.throttle_value = throttle_val;
        msg.brake_value = brake_val;
        drc_driving_control_cmd_t_publish(self->lcm, "DRC_DRIVING_COMMAND", &msg);

        self->throttle_val = throttle_val;
        self->brake_val = brake_val;
        self->hand_steer = steering_input;        
    }
    else{
        self->curr_state = ERROR_NO_VALID_GOAL;
        perform_emergency_stop (self);
    }

    last_utime = self->utime;

    publish_status(self);
    return TRUE;
}

static void
driving_control_destroy (state_t *self)
{
    if (!self)
        return;

    if (self->cost_map_last)
        occ_map_pixel_map_t_destroy (self->cost_map_last);

    if (self->mainloop)
        g_main_loop_unref (self->mainloop);

    free (self);
}


static void usage (int argc, char **argv)
{
    fprintf (stdout, "Usage: %s [options]\n"
             "\n"
             " -v, --verbose    verbose output\n"
             " -a, --arc        Steering based on the turning radius\n"
             " -d, --differential_cmd send differential commands\n"
             " -h, --help    help\n"
             "\n",
             argv[0]);
}


int main (int argc, char **argv) {
   
    setlinebuf(stdout);

    state_t *self = (state_t *) calloc (1, sizeof (state_t));
    self->goal_distance = DEFAULT_GOAL_DISTANCE;
    self->curr_state = IDLE;
    char *optstring = "vdah";
    char c;
    struct option long_opts[] = {
        { "verbose", no_argument, 0, 'v'},
        { "differential_cmd", no_argument, 0, 'd'},
        { "arc", no_argument, 0, 'a'},
        { 0, 0, 0, 0 }
    };

    int arc = 0;

    while ((c = getopt_long (argc, argv, optstring, long_opts, 0)) >= 0)
        {
            switch (c) {
            case 'd':
                fprintf(stderr, "Using differential commands\n");
                self->use_differential_angle = 1;//goal_distance = strtod (optarg, NULL);
                break;
            case 'a':
                fprintf(stderr, "Setting arc based controller - calculating the exact steering angle needed to drive\n");
                arc = 1;
                break;
            case 'v':
                self->verbose = 1;
                break;
            default:
                usage (argc, argv);
                return 1;
            };
        }

    self->accelerator_utime = -1;
    self->stop_utime = -1;

    self->lcm = bot_lcm_get_global (NULL);
    if (!self->lcm) {
        fprintf (stderr, "Error getting LCM\n");
        //goto fail;
        return -1;
    }

    self->param = bot_param_new_from_server (self->lcm, 0);
    if (!self->param) {
        fprintf (stderr, "Error getting BotParam\n");
        //goto fail;
        return -1;
    }

    self->frames = bot_frames_new (self->lcm, self->param);
    if (!self->frames) {
        fprintf (stderr, "Error getting BotFrames\n");
        //goto fail;
        return -1;
    }

    self->last_driving_cmd = NULL;
    self->drive_duration = 0;
    // Get default parameters
    self->goal_distance = 10.0;//bot_param_get_double_or_fail (self->param, "driving.control.lookahead_distance_default");
    self->kp_steer = 5;//bot_param_get_double_or_fail (self->param, "driving.control.kp_steer_default");


    self->lcmgl_goal = bot_lcmgl_init (self->lcm, "DRIVING_GOAL");
    self->lcmgl_arc = bot_lcmgl_init (self->lcm, "DRIVING_ARC");

    self->lcmgl_rays = bot_lcmgl_init (self->lcm, "DRIVING_RAYS");

    drc_utime_t_subscribe(self->lcm, "ROBOT_UTIME", on_utime, self);

    occ_map_pixel_map_t_subscribe (self->lcm, "TERRAIN_DIST_MAP", on_terrain_dist_map, self);
    drc_driving_control_params_t_subscribe (self->lcm, "DRIVING_CONTROL_PARAMS_DESIRED", on_driving_control_params, self);

    drc_driving_affordance_status_t_subscribe (self->lcm, "DRIVING_STEERING_ACTUATION_STATUS", on_driving_affordance_status, self);
    drc_driving_affordance_status_t_subscribe (self->lcm, "DRIVING_GAS_ACTUATION_STATUS", on_driving_affordance_status, self);
    drc_driving_affordance_status_t_subscribe (self->lcm, "DRIVING_BRAKE_ACTUATION_STATUS", on_driving_affordance_status, self);
    
    drc_driving_cmd_t_subscribe(self->lcm, "DRIVING_CONTROLLER_COMMAND", on_driving_command, self);

    perception_pointing_vector_t_subscribe(self->lcm, "OBJECT_BEARING", on_bearing_vec, self);

    self->mainloop = g_main_loop_new (NULL, TRUE);
    
    self->timer_period = TIMER_PERIOD_MSEC;
    
    if(self->use_differential_angle == 1){
        self->timer_period = TIMER_PERIOD_MSEC_LONG;
    }

    // Control timer
    self->controller_timer_id = g_timeout_add (self->timer_period, on_controller_timer, self);
    
    g_timeout_add (STATUS_TIMER_PERIOD_MSEC, status_timer, self);

    g_timeout_add (STATE_UPDATE_TIMER_PERIOD_MSEC, publish_state_timer, self);
    
    // Connect LCM to the mainloop
    bot_glib_mainloop_attach_lcm (self->lcm);

    g_main_loop_run (self->mainloop);

    return 1;
}