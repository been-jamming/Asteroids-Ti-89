#define USE_TI89
#define SAVE_SCREEN

#include <tigcclib.h>
#include "extgraph.h"

#define true 1
#define false 0

/*
Asteroids Ti89

by Ben Jones
3/20/2018

Spring break programming project
*/

typedef unsigned char bool;

//32 bit fixed point structure, 16 fractional bits
typedef struct{
	long int value;
} fixed16;

typedef struct{
	fixed16 x;
	fixed16 y;
} vector;

typedef struct{
	vector *points;
	vector velocity;
	vector center;
	unsigned char num_points;
	unsigned char mass;
	bool collision;
	bool out_of_bounds;
	//Might add rotation, we'll see (could use a table of constant rotation intervals)
} asteroid;

typedef struct{
	int x;
	int y;
	unsigned char lifetime;
	unsigned char frame;
} explosion;

typedef struct{
	vector position;
	vector direction;
	bool active;
} bullet;

fixed16 add_fixed(fixed16 a, fixed16 b){
	return (fixed16) {.value = a.value + b.value};
}

fixed16 subtract_fixed(fixed16 a, fixed16 b){
	return (fixed16) {.value = a.value - b.value};
}

fixed16 multiply_fixed(fixed16 a, fixed16 b){
	return (fixed16) {.value = (a.value>>8)*(b.value>>8)};
}

vector scale_vector(vector a, fixed16 b){
	return (vector) {.x = multiply_fixed(a.x, b), .y = multiply_fixed(a.y, b)};
}

fixed16 dot_product(vector a, vector b){
	return add_fixed(multiply_fixed(a.x, b.x), multiply_fixed(a.y, b.y));
}

fixed16 cross_product(vector a, vector b){
	return subtract_fixed(multiply_fixed(a.x, b.y), multiply_fixed(a.y, b.x));
}

fixed16 sin10;
fixed16 cos10;

vector add_vector(vector a, vector b){
	return (vector) {.x = add_fixed(a.x, b.x), .y = add_fixed(a.y, b.y)};
}

vector subtract_vector(vector a, vector b){
	return (vector) {.x = subtract_fixed(a.x, b.x), .y = subtract_fixed(a.y, b.y)};
}

vector rotate_vector_10deg(vector center, vector point){
	vector translated;
	vector output;
	translated = subtract_vector(point, center);
	output.x = subtract_fixed(multiply_fixed(translated.x, cos10), multiply_fixed(translated.y, sin10));
	output.y = add_fixed(multiply_fixed(translated.x, sin10), multiply_fixed(translated.y, cos10));
	output = add_vector(output, center);
	return output;
}

vector rotate_vector_neg10deg(vector center, vector point){
	vector translated;
	vector output;
	translated = subtract_vector(point, center);
	output.x = add_fixed(multiply_fixed(translated.x, cos10), multiply_fixed(translated.y, sin10));
	output.y = subtract_fixed(multiply_fixed(translated.y, cos10), multiply_fixed(translated.x, sin10));
	output = add_vector(output, center);
	return output;
}

unsigned long int long_random(unsigned long int max){
	short part1;
	short part2;
	if(max >= 1UL<<17){
		return ((unsigned long int) random(max>>16))<<16 | random(1UL<<16);
	} else {
		return random(max);
	}
}

unsigned short int explosion_sprite0[12] = {
	0b0000000000000000,
	0b0000000000000000,
	0b0000000000000000,
	0b0001100110000000,
	0b0001011010000000,
	0b0000111100000000,
	0b0000111100000000,
	0b0001011010000000,
	0b0001100110000000,
	0b0000000000000000,
	0b0000000000000000,
	0b0000000000000000
};

unsigned short int explosion_sprite1[12] = {
	0b0000000000000000,
	0b0000000000000000,
	0b0000111100000000,
	0b0001100110000000,
	0b0010011001000000,
	0b0010100101000000,
	0b0010100101000000,
	0b0010011001000000,
	0b0001100110000000,
	0b0000111100000000,
	0b0000000000000000,
	0b0000000000000000
};

unsigned short int explosion_sprite2[12] = {
	0b0000000000000000,
	0b0000011000000000,
	0b0001100110000000,
	0b0011000011000000,
	0b0010011001000000,
	0b0100100100100000,
	0b0100100100100000,
	0b0010011001000000,
	0b0011000011000000,
	0b0001100110000000,
	0b0000011000000000,
	0b0000000000000000
};

unsigned short *clip_coords;
asteroid asteroids[20];
explosion explosions[20];
bullet bullets[20];
char asteroid_index;
char explosion_index;
char bullet_index;
fixed16 new_center_x;
fixed16 new_center_y;
char virtual[LCD_SIZE];
void *kbq;
INT_HANDLER old_int_5 = NULL;
volatile bool quit;
volatile bool do_update;
volatile bool do_add_asteroid;
volatile bool left_key;
volatile bool right_key;
volatile bool up_key;
volatile unsigned int frame;
volatile unsigned int death_frames;
volatile bool game_over;
int rotation;

vector player_point1;
vector player_point2;
vector player_point3;
vector player_point4;
vector player_bullet1;
vector player_bullet2;
vector player_direction;
vector player_velocity;

void discard_asteroid(unsigned char i){
	asteroid temp;
	asteroid_index--;
	if(asteroid_index < 0){
		asteroid_index += 20;
	}
	temp = asteroids[asteroid_index];
	asteroids[asteroid_index] = asteroids[i];
	asteroids[i] = temp;
	asteroids[asteroid_index].num_points = 0;
}

//Compare vectors by their angle
short int compare_vectors(vector a, vector b){
	if((b.x.value - new_center_x.value) == 0){
		if(b.y.value > 0){
			if(a.x.value > 0 && a.y.value > 0){
				return -1;
			} else {
				return 1;
			}
		} else {
			if(a.y.value > 0 || (a.y.value < 0 && a.x.value < 0)){
				return -1;
			} else {
				return 1;
			}
		}
	} else if((a.x.value - new_center_x.value) > 0 && (a.y.value - new_center_y.value) >= 0){
		if((b.x.value - new_center_x.value) < 0 || (b.y.value - new_center_y.value) < 0){
			return -1;
		} else if(((a.y.value - new_center_y.value)<<8)/(a.x.value - new_center_x.value) > ((b.y.value - new_center_y.value)<<8)/(b.x.value - new_center_x.value)){
			return 1;
		} else {
			return -1;
		}
	} else if((a.x.value - new_center_x.value) < 0 && (a.y.value - new_center_y.value) >= 0){
		if((b.x.value - new_center_x.value) > 0 && (b.y.value - new_center_y.value) > 0){
			return 1;
		} else if((b.x.value - new_center_x.value) < 0 && (b.y.value - new_center_y.value) > 0 && ((a.y.value - new_center_y.value)<<8)/(a.x.value - new_center_x.value) > ((b.y.value - new_center_y.value)<<8)/(b.x.value - new_center_x.value)){
			return 1;
		} else {
			return -1;
		}
	} else if((a.x.value - new_center_x.value) < 0 && (a.y.value - new_center_y.value) < 0){
		if((b.y.value - new_center_y.value) > 0){
			return 1;
		} else if((b.x.value - new_center_x.value) < 0 && (b.y.value - new_center_y.value) < 0 && ((a.y.value - new_center_y.value)<<8)/(a.x.value - new_center_x.value) > ((b.y.value - new_center_y.value)<<8)/(b.x.value - new_center_x.value)){
			return 1;
		} else {
			return -1;
		}
	} else if((a.x.value - new_center_x.value) > 0 && (a.y.value - new_center_y.value) < 0){
		if((b.y.value - new_center_y.value) > 0 || ((b.x.value - new_center_x.value) < 0 && (b.y.value - new_center_y.value) < 0)){
			return 1;
		} else if((b.x.value - new_center_x.value) > 0 && (b.y.value - new_center_y.value) < 0 && ((a.y.value - new_center_y.value)<<8)/(a.x.value - new_center_x.value) > ((b.y.value - new_center_y.value)<<8)/(b.x.value - new_center_x.value)){
			return 1;
		} else {
			return -1;
		}
	} else if((a.x.value - new_center_x.value) == 0 && (a.y.value - new_center_y.value) >= 0){
		if((b.x.value - new_center_x.value) > 0 && (b.y.value - new_center_y.value) > 0){
			return 1;
		} else {
			return -1;
		}
	} else {
		if((b.y.value - new_center_y.value) > 0 || ((b.x.value - new_center_x.value) < 0 && (b.y.value - new_center_y.value) < 0)){
			return 1;
		} else {
			return -1;
		}
	}
}

CALLBACK short int qsort_compare_vectors(const void *a, const void *b){
	return compare_vectors(*((vector *) a), *((vector *) b));
}

asteroid create_random_asteroid(unsigned char mass, int center_x, int center_y, vector velocity){
	asteroid output;
	unsigned char num_points;
	unsigned char i;
	new_center_x.value = 0;
	new_center_y.value = 0;
	vector center;
	long int rand_x;
	long int rand_y;
	if(mass < 3){
		mass = 3;
	}
	num_points = random(mass);
	if(num_points < 3){
		num_points = 3;
	}
	center.x = (fixed16) {.value = ((long int) center_x)<<16};
	center.y = (fixed16) {.value = ((long int) center_y)<<16};
	
	output.mass = mass;
	output.num_points = num_points;
	output.points = malloc(sizeof(vector)*num_points);
	for(i = 0; i < num_points; i++){
		rand_x = random(mass<<1) + mass;
		rand_y = random(mass<<1) + mass;
		if(random(2)){
			rand_x = -rand_x;
		}
		if(random(2)){
			rand_y = -rand_y;
		}
		rand_x <<= 16;
		rand_y <<= 16;
		output.points[i].x = (fixed16) {.value = center.x.value + rand_x};
		output.points[i].y = (fixed16) {.value = center.y.value + rand_y};
		new_center_x = add_fixed(new_center_x, output.points[i].x);
		new_center_y = add_fixed(new_center_y, output.points[i].y);
	}
	new_center_x.value /= ((long int) num_points)<<16;
	new_center_y.value /= ((long int) num_points)<<16;
	new_center_x.value <<= 16;
	new_center_y.value <<= 16;
	qsort(output.points, num_points, sizeof(vector), qsort_compare_vectors);
	output.velocity = velocity;
	output.center = (vector) {.x = new_center_x, .y = new_center_y};
	output.collision = false;
	output.out_of_bounds = false;
	return output;
}

void spawn_explosion(int x, int y, unsigned char lifetime){
	explosions[explosion_index].x = x;
	explosions[explosion_index].y = y;
	explosions[explosion_index].lifetime = lifetime;
	explosions[explosion_index].frame = 0;
	explosion_index = (explosion_index + 1)%20;
}

vector create_random_velocity(){
	vector output;
	output.x = (fixed16) {.value = rand()%(1UL<<15)};
	if(random(2)){
		output.x.value += 1UL<<15;
	} else {
		output.x.value *= -1;
		output.x.value -= 1UL<<15;
	}
	output.y = (fixed16) {.value = rand()%(1UL<<15)};
	if(random(2)){
		output.y.value += 1UL<<15;
	} else {
		output.y.value *= -1;
		output.y.value -= 1UL<<15;
	}
	return output;
}

void split_asteroid(unsigned char i){
	long int new_center_dx;
	long int new_center_dy;
	long int new_center_x1;
	long int new_center_y1;
	long int new_center_x2;
	long int new_center_y2;
	vector velocity;
	asteroid child1;
	asteroid child2;
	unsigned char mass;
	unsigned char new_mass;
	
	mass = asteroids[i].mass;
	
	new_center_dx = random(mass<<1) + mass;
	new_center_dy = random(mass<<1) + mass;
	if(random(2)){
		new_center_dx = -new_center_dx;
	}
	if(random(2)){
		new_center_dy = -new_center_dy;
	}
	new_center_dx <<= 16;
	new_center_dy <<= 16;
	
	new_center_x1 = asteroids[i].center.x.value + new_center_dx;
	new_center_y1 = asteroids[i].center.y.value + new_center_dy;
	new_center_x2 = asteroids[i].center.x.value - new_center_dx;
	new_center_y2 = asteroids[i].center.y.value - new_center_dy;
	new_mass = asteroids[i].mass>>1;
	velocity = asteroids[i].velocity;
	spawn_explosion(asteroids[i].center.x.value>>16, asteroids[i].center.y.value>>16, asteroids[i].mass<<1);
	discard_asteroid(i);
	if(asteroids[i].mass != 3){
		if(new_mass < 3){
			new_mass = 3;
		}
		child1 = create_random_asteroid(new_mass, new_center_x1>>16, new_center_y1>>16, create_random_velocity());
		if(asteroids[asteroid_index].points != (vector *) 0){
			free(asteroids[asteroid_index].points);
		}
		asteroids[asteroid_index] = child1;
		asteroid_index = (asteroid_index + 1)%20;
		if(asteroids[asteroid_index].num_points == 0){
			child2 = create_random_asteroid(new_mass, new_center_x2>>16, new_center_y2>>16, create_random_velocity());
			if(asteroids[asteroid_index].points != (vector *) 0){
				free(asteroids[asteroid_index].points);
			}
			asteroids[asteroid_index] = child2;
			asteroid_index = (asteroid_index + 1)%20;
		}
	}
}

void add_asteroid(){
	unsigned char mass;
	asteroid new_asteroid;
	unsigned char direction;
	int center_x;
	int center_y;
	long int velocity_x;
	long int velocity_y;
	if(asteroids[asteroid_index].num_points == 0){
		mass = random(5) + 5;
		direction = random(4);
		if(direction == 0){//Top
			center_x = random(160);
			center_y = -80;
			velocity_x = random(5) - 2;
			velocity_y = random(2) + 1;
		} else if(direction == 1){//Bottom
			center_x = random(160);
			center_y = 180;
			velocity_x = random(5) - 2;
			velocity_y = -random(2) - 1;
		} else if(direction == 2){//Left
			center_x = -80;
			center_y = random(100);
			velocity_x = random(2) + 1;
			velocity_y = random(5) - 2;
		} else {//Right
			center_x = 240;
			center_y = random(100);
			velocity_x = -random(2) - 1;
			velocity_y = random(5) - 2;
		}
		new_asteroid = create_random_asteroid(mass, center_x, center_y, (vector) {.x = (fixed16) {.value = velocity_x<<14}, .y = (fixed16) {.value = velocity_y<<14}});
		if(asteroids[asteroid_index].points != (vector *) 0){
			free(asteroids[asteroid_index].points);
		}
		asteroids[asteroid_index] = new_asteroid;
		asteroid_index = (asteroid_index + 1)%20;
	}
}

void shoot_bullet(){
	vector position;
	vector direction;
	position = player_bullet1;
	direction = subtract_vector(player_bullet2, player_bullet1);
	bullets[bullet_index].position = position;
	bullets[bullet_index].direction = direction;
	bullets[bullet_index].active = true;
	bullet_index = (bullet_index + 1)%20;
}

void move_asteroids(){
	unsigned char i;
	unsigned char j;
	for(i = 0; i < 20; i++){
		for(j = 0; j < asteroids[i].num_points; j++){
			asteroids[i].points[j] = add_vector(asteroids[i].points[j], add_vector(asteroids[i].velocity, player_velocity));
		}
		asteroids[i].center = add_vector(asteroids[i].center, add_vector(asteroids[i].velocity, player_velocity));
	}
}

void move_bullets(){
	unsigned char i;
	for(i = 0; i < 20; i++){
		if(bullets[i].active){
			bullets[i].position = add_vector(bullets[i].position, scale_vector(bullets[i].direction, (fixed16) {.value = 1L<<15}));
			if(bullets[i].position.x.value>>16 < 0 || bullets[i].position.x.value>>16 > 160 || bullets[i].position.y.value>>16 < 0 || bullets[i].position.y.value>>16 > 100){
				bullets[i].active = false;
			}
		}
	}
}

bool display_asteroid(asteroid *a){
	vector last_point;
	unsigned char i;
	bool output;
	output = false;
	if(a->num_points > 0){
		last_point = a->points[0];
		if(last_point.x.value>>16 < -160 || last_point.x.value>>16 > 320 || last_point.y.value>>16 < -160 || last_point.y.value>>16 > 260){
			a->out_of_bounds = true;
		}
		for(i = 1; i < a->num_points; i++){
			if(ClipLine_R(last_point.x.value>>16, last_point.y.value>>16, a->points[i].x.value>>16, a->points[i].y.value>>16, clip_coords) != NULL){
				if(FastTestLine_BE_R(virtual, clip_coords[0], clip_coords[1], clip_coords[2], clip_coords[3])){
					output = true;
					break;
				}
			}
			last_point = a->points[i];
		}
		last_point = a->points[0];
		for(i = 1; i < a->num_points; i++){
			ClipDrawLine_R(last_point.x.value>>16, last_point.y.value>>16, a->points[i].x.value>>16, a->points[i].y.value>>16, clip_coords, A_NORMAL, virtual, FastDrawLine_R);
			last_point = a->points[i];
		}
		ClipDrawLine_R(last_point.x.value>>16, last_point.y.value>>16, a->points[0].x.value>>16, a->points[0].y.value>>16, clip_coords, A_NORMAL, virtual, FastDrawLine_R);
	}
	return output;
}

void display_asteroids(){
	unsigned char i;
	for(i = 0; i < 20; i++){
		asteroids[i].collision = display_asteroid(asteroids + i);
	}
}

void display_explosion(unsigned char i){
	if(explosions[i].lifetime){
		if(explosions[i].frame < 3){
			ClipSprite16_OR_R(explosions[i].x, explosions[i].y, 12, explosion_sprite0, virtual);
		} else if(explosions[i].frame < 6){
			ClipSprite16_OR_R(explosions[i].x, explosions[i].y, 12, explosion_sprite1, virtual);
		} else {
			ClipSprite16_OR_R(explosions[i].x, explosions[i].y, 12, explosion_sprite2, virtual);
		}
		explosions[i].lifetime--;
		explosions[i].frame++;
	}
}

void display_explosions(){
	unsigned char i;
	for(i = 0; i < 20; i++){
		display_explosion(i);
	}
}

void display_bullet(unsigned char i){
	vector position;
	vector next_position;
	int x0;
	int y0;
	int x1;
	int y1;
	if(bullets[i].active){
		position = bullets[i].position;
		next_position = add_vector(position, bullets[i].direction);
		x0 = position.x.value>>16;
		y0 = position.y.value>>16;
		x1 = next_position.x.value>>16;
		y1 = next_position.y.value>>16;
		ClipDrawLine_R(x0, y0, x1, y1, clip_coords, A_NORMAL, virtual, FastDrawLine_R);
		ClipDrawLine_R(x0 + 1, y0, x1 + 1, y1, clip_coords, A_NORMAL, virtual, FastDrawLine_R);
		ClipDrawLine_R(x0, y0 + 1, x1, y1 + 1, clip_coords, A_NORMAL, virtual, FastDrawLine_R);
	}
}

void display_bullets(){
	unsigned char i;
	for(i = 0; i < 20; i++){
		display_bullet(i);
	}
}

void display_player(){
	unsigned char x0;
	unsigned char y0;
	unsigned char x1;
	unsigned char y1;
	unsigned char x2;
	unsigned char y2;
	unsigned char x3;
	unsigned char y3;
	if(!game_over){
		x0 = player_point1.x.value>>16;
		y0 = player_point1.y.value>>16;
		x1 = player_point2.x.value>>16;
		y1 = player_point2.y.value>>16;
		x2 = player_point3.x.value>>16;
		y2 = player_point3.y.value>>16;
		if(FastTestLine_BE_R(virtual, x0, y0, x1, y1) || FastTestLine_BE_R(virtual, x1, y1, x2, y2) || FastTestLine_BE_R(virtual, x2, y2, x0, y0)){
			game_over = true;
			death_frames = 25;
			spawn_explosion(80, 50, 16);
		} else {
			FastDrawLine(virtual, x0, y0, x1, y1, A_NORMAL);
			FastDrawLine(virtual, x1, y1, x2, y2, A_NORMAL);
			FastDrawLine(virtual, x2, y2, x0, y0, A_NORMAL);
			if(up_key){
				x3 = player_point4.x.value>>16;
				y3 = player_point4.y.value>>16;
				FastDrawLine(virtual, x1, y1, x3, y3, A_NORMAL);
				FastDrawLine(virtual, x2, y2, x3, y3, A_NORMAL);
			}
		}
	}
}

void out_of_bounds(){
	unsigned char i;
	for(i = 0; i < 20; i++){
		if(asteroids[i].out_of_bounds){
			discard_asteroid(i);
		}
	}
}

void split_asteroids(){
	unsigned char i;
	for(i = 0; i < 20;i++){
		if(asteroids[i].collision){
			split_asteroid(i);
		}
	}
}

void free_asteroid(asteroid *a){
	free(a->points);
	free(a);
}

void initialize_asteroids(){
	unsigned char i;
	for(i = 0; i < 20; i++){
		asteroids[i] = (asteroid) {.points = (vector *) 0, .velocity = (vector) {.x = (fixed16) {.value = 0}, .y = (fixed16) {.value = 0}}, .center = (vector) {.x = (fixed16) {.value = 0}, .y = (fixed16) {.value = 0}}, .num_points = 0, .collision = false, .out_of_bounds = false};
	}
}

void initialize_explosions(){
	unsigned char i;
	for(i = 0; i < 20; i++){
		explosions[i] = (explosion) {.x = 0, .y = 0, .lifetime = 0, .frame = 0};
	}
}

void initialize_bullets(){
	unsigned char i;
	for(i = 0; i < 20; i++){
		bullets[i] = (bullet) {.position = (vector) {.x = (fixed16) {.value = 0}, .y = (fixed16) {.value = 0}}, .direction = (vector) {.x = (fixed16) {.value = 0}, .y = (fixed16) {.value = 0}}, .active = false};
	}
}

DEFINE_INT_HANDLER (frame_update){
	unsigned int key;
	while(!OSdequeue(&key, kbq)){
		if(key == KEY_ESC){
			quit = true;
		} else if(key == KEY_DOWN){
			shoot_bullet();
		}
	}
	if(_keytest(RR_LEFT)){
		left_key = true;
	} else {
		left_key = false;
	}
	if(_keytest(RR_RIGHT)){
		right_key = true;
	} else {
		right_key = false;
	}
	if(_keytest(RR_UP)){
		up_key = true;
	} else {
		up_key = false;
	}
	if(!(frame%40)){
		do_add_asteroid = true;
	}
	do_update = true;
	frame++;
	if(death_frames){
		death_frames--;
	}
	ExecuteHandler(old_int_5);
}

void free_asteroids(){
	unsigned char i;
	for(i = 0; i < 20; i++){
		if(asteroids[i].points != (vector *) 0){
			free(asteroids[i].points);
		}
	}
}

void _main(){
	clrscr();
	quit = false;
	do_update = false;
	do_add_asteroid = false;
	left_key = false;
	right_key = false;
	up_key = false;
	rotation = 0;
	
	bool dropping_frames;
	vector center_screen;
	center_screen = (vector) {.x = (fixed16) {.value = 80L<<16}, .y = (fixed16) {.value = 50L<<16}};
	
	dropping_frames = false;
	sin10 = (fixed16) {.value = 11380L};
	cos10 = (fixed16) {.value = 64540L};
	player_point1 = (vector) {.x = (fixed16) {.value = 80L<<16}, .y = (fixed16) {.value = 45L<<16}};
	player_point2 = (vector) {.x = (fixed16) {.value = 75L<<16}, .y = (fixed16) {.value = 55L<<16}};
	player_point3 = (vector) {.x = (fixed16) {.value = 85L<<16}, .y = (fixed16) {.value = 55L<<16}};
	player_point4 = (vector) {.x = (fixed16) {.value = 80L<<16}, .y = (fixed16) {.value = 60L<<16}};
	player_bullet1 = (vector) {.x = (fixed16) {.value = 80L<<16}, .y = (fixed16) {.value = 45L<<16}};
	player_bullet2 = (vector) {.x = (fixed16) {.value = 80L<<16}, .y = (fixed16) {.value = 33L<<16}};
	player_direction = (vector) {.x = (fixed16) {.value = 0}, .y = (fixed16) {.value = 1L<<13}};
	player_velocity = (vector) {.x = (fixed16) {.value = 0}, .y = (fixed16) {.value = 0}};
	asteroid_index = 0;
	explosion_index = 0;
	bullet_index = 0;
	frame = 0;
	randomize();
	initialize_asteroids();
	initialize_explosions();
	clip_coords = malloc(sizeof(short int)*4);
	kbq = kbd_queue();
	old_int_5 = GetIntVec(AUTO_INT_5);
	SetIntVec(AUTO_INT_5, frame_update);
	while(!quit){
		if(do_update){
			do_update = false;
			if(left_key){
				rotation += 10;
				if(rotation >= 360){
					rotation -= 360;
				}
				if(rotation <= -360){
					rotation += 360;
				}
				if(rotation == 0){
					player_point1 = (vector) {.x = (fixed16) {.value = 80L<<16}, .y = (fixed16) {.value = 45L<<16}};
					player_point2 = (vector) {.x = (fixed16) {.value = 75L<<16}, .y = (fixed16) {.value = 55L<<16}};
					player_point3 = (vector) {.x = (fixed16) {.value = 85L<<16}, .y = (fixed16) {.value = 55L<<16}};
					player_point4 = (vector) {.x = (fixed16) {.value = 80L<<16}, .y = (fixed16) {.value = 60L<<16}};
					player_bullet1 = (vector) {.x = (fixed16) {.value = 80L<<16}, .y = (fixed16) {.value = 45L<<16}};
					player_bullet2 = (vector) {.x = (fixed16) {.value = 80L<<16}, .y = (fixed16) {.value = 33L<<16}};
					player_direction = (vector) {.x = (fixed16) {.value = 0}, .y = (fixed16) {.value = 1L<<13}};
				} else {
					player_point1 = rotate_vector_neg10deg(center_screen, player_point1);
					player_point2 = rotate_vector_neg10deg(center_screen, player_point2);
					player_point3 = rotate_vector_neg10deg(center_screen, player_point3);
					player_point4 = rotate_vector_neg10deg(center_screen, player_point4);
					player_bullet1 = rotate_vector_neg10deg(center_screen, player_bullet1);
					player_bullet2 = rotate_vector_neg10deg(center_screen, player_bullet2);
					player_direction = rotate_vector_neg10deg((vector) {.x = (fixed16) {.value = 0}, .y = (fixed16) {.value = 0}}, player_direction);
				}
			}
			if(right_key){
				rotation -= 10;
				if(rotation >= 360){
					rotation -= 360;
				}
				if(rotation <= -360){
					rotation += 360;
				}
				if(rotation == 0){
					player_point1 = (vector) {.x = (fixed16) {.value = 80L<<16}, .y = (fixed16) {.value = 45L<<16}};
					player_point2 = (vector) {.x = (fixed16) {.value = 75L<<16}, .y = (fixed16) {.value = 55L<<16}};
					player_point3 = (vector) {.x = (fixed16) {.value = 85L<<16}, .y = (fixed16) {.value = 55L<<16}};
					player_point4 = (vector) {.x = (fixed16) {.value = 80L<<16}, .y = (fixed16) {.value = 60L<<16}};
					player_bullet1 = (vector) {.x = (fixed16) {.value = 80L<<16}, .y = (fixed16) {.value = 45L<<16}};
					player_bullet2 = (vector) {.x = (fixed16) {.value = 80L<<16}, .y = (fixed16) {.value = 33L<<16}};
					player_direction = (vector) {.x = (fixed16) {.value = 0}, .y = (fixed16) {.value = 1L<<13}};
				} else {
					player_point1 = rotate_vector_10deg(center_screen, player_point1);
					player_point2 = rotate_vector_10deg(center_screen, player_point2);
					player_point3 = rotate_vector_10deg(center_screen, player_point3);
					player_point4 = rotate_vector_10deg(center_screen, player_point4);
					player_bullet1 = rotate_vector_10deg(center_screen, player_bullet1);
					player_bullet2 = rotate_vector_10deg(center_screen, player_bullet2);
					player_direction = rotate_vector_10deg((vector) {.x = (fixed16) {.value = 0}, .y = (fixed16) {.value = 0}}, player_direction);
				}
			}
			if(up_key){
				player_velocity = add_vector(player_velocity, player_direction);
			}
			out_of_bounds();
			split_asteroids();
			move_asteroids();
			move_bullets();
			FastClearScreen_R(virtual);
			display_bullets();
			display_asteroids();
			display_explosions();
			display_player();
			memcpy(LCD_MEM, virtual, LCD_SIZE);
			if(do_update || dropping_frames){
				DrawStr(0, 0, "Dropping Frames!", A_REVERSE);
				dropping_frames = true;
			}
			if(do_add_asteroid){
				add_asteroid();
			}
			if(game_over && !death_frames){
				clrscr();
				printf("Game Over!\nScore: %d sec", frame/18);
				ngetchx();
				quit = true;
			}
		}
	}
	free(clip_coords);
	free_asteroids();
	SetIntVec(AUTO_INT_5, old_int_5);
}

//800 lines actually :)
