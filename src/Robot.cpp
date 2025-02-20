#include "main.h"
#include "Robot.h"
#include "system/json.hpp"
#include "system/Serial.h"
#include "PD.h"
// #include "PD.cpp"
#include <map>
#include <cmath>
#include <atomic>
#include <vector>
#include <chrono>
#include <unordered_map>
#include <deque>
using namespace pros;
using namespace std;


std::map<std::string, std::unique_ptr<pros::Task>> Robot::tasks;

Controller Robot::master(E_CONTROLLER_MASTER);
Motor Robot::FLT(2, true); //front left top
Motor Robot::FLB(1); //front left bottom
Motor Robot::FRT(9); //front right top
Motor Robot::FRB(10, true); //front right bottom
Motor Robot::BRT(20, true); //back right top
Motor Robot::BRB(19); //back right bottom
Motor Robot::BLT(11); //back left top
Motor Robot::BLB(12, true); //back left bottom

Motor Robot::angler(17);
Motor Robot::conveyor(18);

Imu Robot::IMU(15);
ADIEncoder Robot::LE(5, 6);
ADIEncoder Robot::RE(3, 4);
ADIEncoder Robot::BE(7, 8);
ADIDigitalOut Robot::piston(2);
ADIAnalogIn Robot::potentiometer(1);
Distance Robot::dist(9);

PD Robot::power_PD(.32, 5, 0);
PD Robot::strafe_PD(.17, .3, 0);
PD Robot::turn_PD(2.4, 1, 0);

std::atomic<double> Robot::y = 0;
std::atomic<double> Robot::x = 0;
std::atomic<double> Robot::new_x = 0;
std::atomic<double> Robot::new_y = 0;
std::atomic<double> Robot::heading = 0;

double Robot::offset_back = 2.875;
double Robot::offset_middle = 5.0;
double pi = 3.141592653589793238;
double Robot::wheel_circumference = 2.75 * pi;
double inches_to_encoder = 41.669
double meters_to_inches = 39.3701

double angle_threshold = 5;
double depth_threshold1 = 200;
double depth_threshold2 = 50;
double depth_coefficient1 = .2;
double depth_coefficient2 = .02;


void Robot::receive_mogo(nlohmann::json msg) {

    double depth_coefficient = depth_coefficient1;
    string msgS = msg.dump();
    std::size_t found = msgS.find(",");

    double lidar_depth = std::stod(msgS.substr(1, found - 1)); // Gets depth to an object in meters
    double angle = std::stod(msgS.substr(found + 1, msgS.size() - found - 1)); // Gets rotation from a mogo in degrees
    double phi = IMU.get_rotation() * pi / 180; // Gets current rotation in radians
    double depth;

    heading = (IMU.get_rotation() - angle); // Determines target heading by using difference between current rotation and desired rotation
    bool movement_over = false;

    if (abs(angle) < angle_threshold){ // If the angle the mogo is at is less than 5 degrees away from the center
        do {
            depth = dist.get(); // Get depth using sensor (not the camera). Returns value in milimeteres
            double change = depth * depth_coefficient;
            new_y = (float)new_y + change * cos(phi); // Moves forward a distance equivlent to (the distance the mogo is away) * (arbitary coefficient)
            new_x = (float)new_x - change * sin(phi);
            if (depth < depth_threshold1 && !movement_over){ // If we are less than 200 milimeters away from the mogo and haven't stopped, stop.
                lib7405x::Serial::Instance()->send(lib7405x::Serial::STDOUT, "#stop#");
                movement_over = true;
            }
            delay(5);
            depth_coefficient = depth_coefficient2; // Change the depth coefficient so we make smaller changes in movement after looping a single time after we recieve a mogo position.
        } while (depth < depth_threshold1 && depth > depth_threshold2); // Loop while the depth is between .2 and .05 meters
    }

    if (movement_over){
        new_y = (float)y; // Stop moving
        new_x = (float)x;
        lcd::print(7, "MOGO REACHED");
    }
    lcd::print(5, "X: %f Y: %f", (float)new_x, (float)new_y);
    lcd::print(6, "Heading: %f Angle: %f", (float)heading, (float)angle);
}

void Robot::reset_PD() {
    power_PD.reset();
    strafe_PD.reset();
    turn_PD.reset();
}

void Robot::drive(void *ptr) {
    while (true) {
        lcd::print(1,"Potentiometer %d", potentiometer.get_value());
        int power = master.get_analog(ANALOG_LEFT_Y);
        int strafe = master.get_analog(ANALOG_LEFT_X);
        int turn = master.get_analog(ANALOG_RIGHT_X);

        bool angler_forward = master.get_digital(DIGITAL_L1);
        bool angler_backward = master.get_digital(DIGITAL_L2);

        bool piston_open = master.get_digital(DIGITAL_A);
        bool piston_close = master.get_digital(DIGITAL_B);
 
        bool conveyor_forward = master.get_digital(DIGITAL_R1);
        bool conveyor_backward = master.get_digital(DIGITAL_R2);

        if (angler_forward){
            while(potentiometer.get_value()<3710){
                angler = 50;
            }   
            angler = 0;
        } 
        else if (angler_backward){
            while(potentiometer.get_value()>2330){
                angler = -50;
            }
            angler = 0;
        } 
        else angler = 0;

        if (piston_open) piston.set_value(true);
        else if (piston_close) piston.set_value(false);
        
        if (conveyor_forward) conveyor = 100;
        else if (conveyor_backward) conveyor = -100;
        else conveyor = 0;
        
        mecanum(power, strafe, turn);
        delay(5);
    }
}

void Robot::mecanum(int power, int strafe, int turn) {

    int powers[] {
        power + strafe + turn,
        power - strafe - turn,
        power - strafe + turn, 
        power + strafe - turn
    };

    int max = *max_element(powers, powers + 4); // Finds highest power
    int min = abs(*min_element(powers, powers + 4)); // Finds lowest power

    double true_max = double(std::max(max, min)); // Finds highest magnitude of power
    double scalar = (true_max > 127) ? 127 / true_max : 1; // Scales all motor powers down if there is a motor power higher than the max power
    
    FLT = 0*(power + strafe + turn) * scalar; // But why
    FLB = 0*(power + strafe + turn) * scalar; // But why

    FRT = (power - strafe - turn) * scalar;
    FRB = (power - strafe - turn) * scalar;
    BLT = (power - strafe + turn) * scalar;
    BLB = (power - strafe + turn) * scalar;
    BRT = (power + strafe - turn) * scalar;
    BRB = (power + strafe - turn) * scalar;
}


void Robot::start_task(std::string name, void (*func)(void *)) {
    if (!task_exists(name)) {
        tasks.insert(std::pair<std::string, std::unique_ptr<pros::Task>>(name, std::move(std::make_unique<pros::Task>(func, &x, TASK_PRIORITY_DEFAULT, TASK_STACK_DEPTH_DEFAULT, ""))));
    }
}

bool Robot::task_exists(std::string name) {
    return tasks.find(name) != tasks.end();
}

void Robot::kill_task(std::string name) {
    if (task_exists(name)) {
        tasks.erase(name);
    }
}

void Robot::fps(void *ptr) {
    double last_x = 0;
    double last_y = 0;
    double last_phi = 0;
    double turn_offset_y = 0;
    double turn_offset_x = 0;

    while (true) {
        double cur_phi = IMU.get_rotation() * pi / 180;
        double dphi = cur_phi - last_phi;

        double cur_turn_offset_x = 360 * (offset_back * dphi) / wheel_circumference;
        double cur_turn_offset_y = 360 * (offset_middle * dphi) / wheel_circumference;

        turn_offset_x = turn_offset_x + cur_turn_offset_x;
        turn_offset_y = turn_offset_y + cur_turn_offset_y;

        double cur_y = ((LE.get_value() - turn_offset_y) - (RE.get_value() + turn_offset_y)) / -2;
        double cur_x = BE.get_value() - turn_offset_x;

        double dy = cur_y - last_y;
        double dx = cur_x - last_x;

        double global_dy = dy * std::cos(cur_phi) + dx * std::sin(cur_phi);
        double global_dx = dx * std::cos(cur_phi) - dy * std::sin(cur_phi);

        y = (float)y + global_dy;
        x = (float)x + global_dx;

        //lcd::print(1,"Y: %f - X: %f", (float)y, (float)x, IMU.get_rotation());
        // lcd::print(2, "IMU value: %f", IMU.get_heading());
        // lcd::print(3, "LE: %d RE: %d", LE.get_value(), RE.get_value());
        // lcd::print(4, "BE: %d", BE.get_value());
        //lcd::print(2, "Potentiometer: %d", potentiometer.get_value());

        last_y = cur_y;
        last_x = cur_x;
        last_phi = cur_phi;

        delay(5);
    }
}
void Robot::brake(std::string mode)
{

    if (mode.compare("coast") == 0)
    {
        FLT.set_brake_mode(E_MOTOR_BRAKE_COAST);
        FLB.set_brake_mode(E_MOTOR_BRAKE_COAST);
        FRT.set_brake_mode(E_MOTOR_BRAKE_COAST);
        FRB.set_brake_mode(E_MOTOR_BRAKE_COAST);
        BLT.set_brake_mode(E_MOTOR_BRAKE_COAST);
        BLB.set_brake_mode(E_MOTOR_BRAKE_COAST);
        BRT.set_brake_mode(E_MOTOR_BRAKE_COAST);
        BRB.set_brake_mode(E_MOTOR_BRAKE_COAST);

    }
    else if (mode.compare("hold") == 0)
    {
        FLT.set_brake_mode(E_MOTOR_BRAKE_HOLD);
        FLB.set_brake_mode(E_MOTOR_BRAKE_HOLD);
        FRT.set_brake_mode(E_MOTOR_BRAKE_HOLD);
        FRB.set_brake_mode(E_MOTOR_BRAKE_HOLD);
        BLT.set_brake_mode(E_MOTOR_BRAKE_HOLD);
        BLB.set_brake_mode(E_MOTOR_BRAKE_HOLD);
        BRT.set_brake_mode(E_MOTOR_BRAKE_HOLD);
        BRB.set_brake_mode(E_MOTOR_BRAKE_HOLD);

    }
    else FLT = FLB= FRT = FRB= BLT = BLB = BRT = BRB= 0;
}
void Robot::move_to(void *ptr) 
{
    while (true)
    { 
        double imu_error = -(IMU.get_rotation() - heading); // -(IMU - (IMU - angle)) = -angle    See line 76.
        double y_error = new_y - y; // Change in y
        double x_error = -(new_x - x); // Change in x

        double phi = (IMU.get_rotation()) * pi / 180; // Get current rotation in radians
        double power = power_PD.get_value(y_error * std::cos(phi) + x_error * std::sin(phi)); // PD Madness to find forward value
        double strafe = strafe_PD.get_value(x_error * std::cos(phi) - y_error * std::sin(phi)); // PD Madness to find sideways value
        double turn = turn_PD.get_value(imu_error); // PD Madness to find turn value
        turn = (abs(turn) < 15) ? turn : abs(turn)/turn * 15; // Clamps the turn between -15 and 15 degrees

        mecanum(power, strafe, turn); // Drive to the point

        delay(5);
    }
}



