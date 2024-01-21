#include "world.h"

#include "framework/camera.h"
#include "graphics/mesh.h"
#include "graphics/texture.h"
#include "graphics/shader.h"
#include "framework/input.h"
#include "game.h"
#include "entities/entity_mesh.h"
#include "entities/entity_player.h"
#include "entities/entity_enemy.h"
#include "entities/entity_collider.h"

#include <algorithm>
#include <fstream>

World* World::instance = nullptr;

World::World()
{
	bgColor.set(0.2f, 0.2f, 0.2f, 1.f);

	int width = Game::instance->window_width;
	int height = Game::instance->window_height;

	// Create our camera
	camera = new Camera();
	camera->lookAt(Vector3(0.f, 20.f, -20.f), Vector3(0.f, 0.f, 0.f), Vector3(0.f, 1.f, 0.f)); //position the camera and point to 0,0,0
	camera->setPerspective(60.f, width / (float)height, 0.01f, 1000.f); //set the projection, we want to be perspective

	// Configure 2d camera to render UI elements
	camera2D = new Camera();
	camera2D->view_matrix.setIdentity();
	camera2D->setOrthographic(0, width, height, 0, -1.f, 1.f);

	Material player_mat;
	player_mat.shader = Shader::Get("data/shaders/skinning.vs", "data/shaders/phong.fs");
	player_mat.diffuse = Texture::Get("data/meshes/timmy/diffuse.png");
	player_mat.normals = Texture::Get("data/meshes/timmy/normals.png");
	player_mat.Ks = Vector3(0.5);

	Material enemy_mat;
	enemy_mat.shader = Shader::Get("data/shaders/basic.vs", "data/shaders/phong.fs");
	enemy_mat.diffuse = Texture::Get("data/meshes/doozy/Diffuse.png");

	Material level_mat;
	level_mat.shader = Shader::Get("data/shaders/basic.vs", "data/shaders/phong.fs");
	level_mat.diffuse = Texture::Get("data/textures/mario_kart.tga", false);

	Material car_mat;
	car_mat.shader = Shader::Get("data/shaders/basic.vs", "data/shaders/phong.fs");

	Material sky_mat;
	sky_mat.shader = Shader::Get("data/shaders/basic.vs", "data/shaders/texture.fs");
	sky_mat.diffuse = Texture::Get("data/meshes/cielo/cielo.tga");

	Material grass_mat;
	grass_mat.shader = Shader::Get("data/shaders/grass.vs", "data/shaders/phong.fs");
	grass_mat.diffuse = Texture::Get("data/textures/grass.png");
	grass_mat.transparent = true;
	grass_mat.Ks.set(0.f);

	Material landscape_cubemap;
	landscape_cubemap.shader = Shader::Get("data/shaders/basic.vs", "data/shaders/cubemap.fs");
	landscape_cubemap.diffuse = new Texture();
	landscape_cubemap.diffuse->loadCubemap("landscape",
		{
			"data/textures/skybox/right.tga",
			"data/textures/skybox/left.tga",
			"data/textures/skybox/bottom.tga",
			"data/textures/skybox/top.tga",
			"data/textures/skybox/front.tga",
			"data/textures/skybox/back.tga"
		});

	{
		player = new EntityPlayer(
			Mesh::Get("data/meshes/timmy/timmy_sk.MESH"),
			player_mat
		);

		landscape = new EntityMesh(
			Mesh::Get("data/meshes/cubemap/cubemap.ASE"),
			landscape_cubemap
		);
	}

	parseScene("data/myscene.scene");

	std::sort(root.children.begin(), root.children.end(), [](const Entity* lhs, const Entity* rhs)
	{
		return static_cast<const EntityMesh*>(lhs)->material.shader < static_cast<const EntityMesh*>(rhs)->material.shader;
	});

	freeCam = false;

	// Audio::Play3D("data/audio/shot.wav", Vector3(), 1.f, true);
}

void World::updateCamera(float seconds_elapsed)
{
	if (freeCam)
	{
		float mouse_speed = 5.0f;
		float speed = seconds_elapsed * mouse_speed; //the speed is defined by the seconds_elapsed so it goes constant

		//mouse input to rotate the cam
		if (Input::isMousePressed(SDL_BUTTON_RIGHT)) //is left button pressed?
		{
			camera->rotate(Input::mouse_delta.x * 0.005f, Vector3(0.0f, -1.0f, 0.0f));
			camera->rotate(Input::mouse_delta.y * 0.005f, camera->getLocalVector(Vector3(-1.0f, 0.0f, 0.0f)));
		}

		//async input to move the camera around
		if (Input::isKeyPressed(SDL_SCANCODE_LSHIFT)) speed *= 10; //move faster with left shift
		if (Input::isKeyPressed(SDL_SCANCODE_W) || Input::isKeyPressed(SDL_SCANCODE_UP)) camera->move(Vector3(0.0f, 0.0f, 1.0f) * speed);
		if (Input::isKeyPressed(SDL_SCANCODE_S) || Input::isKeyPressed(SDL_SCANCODE_DOWN)) camera->move(Vector3(0.0f, 0.0f, -1.0f) * speed);
		if (Input::isKeyPressed(SDL_SCANCODE_A) || Input::isKeyPressed(SDL_SCANCODE_LEFT)) camera->move(Vector3(1.0f, 0.0f, 0.0f) * speed);
		if (Input::isKeyPressed(SDL_SCANCODE_D) || Input::isKeyPressed(SDL_SCANCODE_RIGHT)) camera->move(Vector3(-1.0f, 0.0f, 0.0f) * speed);
	}
	else {

		// Get mouse deltas
		camera_yaw -= Input::mouse_delta.x * seconds_elapsed * 10.f * DEG2RAD;
		camera_pitch -= Input::mouse_delta.y * seconds_elapsed * 10.f * DEG2RAD;

		// Restrict pitch angle
		camera_pitch = clamp(camera_pitch, -M_PI * 0.3f, M_PI * 0.3f);
	}
}

void World::checkCameraCollisions(Vector3& newEye)
{
	Vector3 origin = camera->center;
	Vector3 direction = (newEye - camera->center);
	testRayToScene(camera->center, direction.normalize(), newEye, Vector3(), true, direction.length());
}

void World::update(float seconds_elapsed)
{
	updateCamera(seconds_elapsed);

	root.update(seconds_elapsed);

	if (!freeCam)
		player->update(seconds_elapsed);
} 

void World::addWayPointFromScreenPos(const Vector2& coord)
{
	// Unproject coords
	Vector3 origin = camera->eye;
	Vector3 direction = camera->getRayDirection(coord.x, coord.y, Game::instance->window_width, Game::instance->window_height);

	// Get collision at 0,0,0 plane
	Vector3 colPoint = RayPlaneCollision(Vector3(), Vector3(0, 1, 0), origin, direction);

	waypoints.push_back(colPoint);
}

bool World::testRayToScene(Vector3 ray_origin, Vector3 ray_direction, Vector3& collision, Vector3& normal, bool get_closest, float max_ray_dist, bool in_object_space)
{
	float closest_dist = INFINITY;
	Vector3 tmpCol;
	bool has_collided = false;

	auto resolve_closest = [&]() {

		has_collided = true;
		float new_distance = tmpCol.distance(ray_origin);
		if (new_distance < closest_dist) {
			collision = tmpCol;
			closest_dist = new_distance;
		}

	};

	for (auto e : root.children)
	{
		EntityCollider* ec = dynamic_cast<EntityCollider*>(e);
		if (!ec) continue;

		if (!ec->isInstanced) {// Single model

			bool result = ec->mesh->testRayCollision(e->model, ray_origin, ray_direction, tmpCol, normal, max_ray_dist, in_object_space);
			if (result)
			{
				if (!get_closest) return true;
				resolve_closest();
			}
		}
		else { // Instanced mesh
			for (int i = 0; i < ec->models.size(); ++i)
			{
				if (!ec->mesh->testRayCollision(ec->models[i], ray_origin, ray_direction, tmpCol, normal, max_ray_dist, in_object_space))
					continue;

				if (!get_closest) return true;
				resolve_closest();
			}
		}
	}

	return has_collided;
}

bool World::parseScene(const char* filename)
{
	// You could fill the map manually to add shader and texture for each mesh
	// If the mesh is not in the map, you can use the MTL file to render its colors

	Material floor_mat;
	floor_mat.diffuse = Texture::Get("data/textures/grass.tga");
	floor_mat.normals = Texture::Get("data/textures/grass_normals.tga");
	floor_mat.shader = Shader::Get("data/shaders/basic.vs", "data/shaders/phong.fs");
	floor_mat.tiling = 20.f;
	floor_mat.Ks.set(0.f);

	Material water_mat;
	water_mat.diffuse = Texture::Get("landscape");
	water_mat.normals = Texture::Get("data/textures/water_normalmap.tga");
	water_mat.shader = Shader::Get("data/shaders/basic.vs", "data/shaders/reflection.fs");

	meshes_to_load["meshes/Floor.obj"] = { floor_mat };
	meshes_to_load["meshes/Water.obj"] = { water_mat };

	std::cout << " + Scene loading: " << filename << "..." << std::endl;

	std::ifstream file(filename);

	if (!file.good()) {
		std::cerr << "Scene [ERROR]" << " File not found!" << std::endl;
		return false;
	}

	std::string scene_info, mesh_name, model_data;
	file >> scene_info; file >> scene_info;
	int mesh_count = 0;

	// Read file line by line and store mesh path and model info in separated variables
	while (file >> mesh_name >> model_data)
	{
		if (mesh_name[0] == '#')
			continue;

		// Get all 16 matrix floats
		std::vector<std::string> tokens = tokenize(model_data, ",");

		// Fill matrix converting chars to floats
		Matrix44 model;
		for (int t = 0; t < tokens.size(); ++t) {
			model.m[t] = (float)atof(tokens[t].c_str());
		}

		// Add model to mesh list (might be instanced!)
		sRenderData& render_data = meshes_to_load[mesh_name];
		render_data.models.push_back(model);
		mesh_count++;
	}

	// Iterate through meshes loaded and create corresponding entities
	for (auto data : meshes_to_load) {

		mesh_name = "data/" + data.first;
		sRenderData& render_data = data.second;

		// No transforms, anything to do here
		if (render_data.models.empty())
			continue;

		Mesh* mesh = Mesh::Get(mesh_name.c_str());
		Material mat = render_data.material;
		EntityCollider* new_entity = new EntityCollider(mesh, mat);

		// Create instanced entity
		if (render_data.models.size() > 1) {
			new_entity->isInstanced = true;
			new_entity->models = render_data.models; // Add all instances
		}
		// Create normal entity
		else {
			new_entity->model = render_data.models[0];
		}

		// Add entity to scene root
		root.addChild(new_entity);
	}

	std::cout << "Scene [OK]" << " Meshes added: " << mesh_count << std::endl;
	return true;
}