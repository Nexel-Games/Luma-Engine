# TP_FirstPerson Luma Project

A first-person shooter template converted from Unreal Engine to Luma Engine.

## Features

- First-person character controller
- Weapon system with projectile shooting
- Camera system
- Input handling (WASD movement, mouse look, fire)
- Basic physics and collision

## Controls

- **WASD**: Movement
- **Mouse**: Look around
- **Left Click**: Fire weapon
- **Space**: Jump
- **Shift**: Sprint

## Project Structure

```
TP_FirstPerson/
├── TP_FirstPerson.ep           # Project configuration
├── FirstPersonScene.scene       # Main scene file
├── assets/
│   ├── models/                 # 3D models
│   ├── materials/              # Material files
│   ├── textures/               # Texture files
│   └── shaders/                # Custom shaders
└── scripts/
    ├── FirstPersonController.cpp
    ├── Weapon.cpp
    └── Projectile.cpp
```

## Getting Started

1. Open the project in Luma Engine editor (TP_FirstPerson.ep)
2. Run the scene to test the first-person controller
3. Customize assets and scripts as needed

## Conversion Notes

This project was converted from Unreal Engine's TP_FirstPerson template. Key changes:
- Unreal C++ classes converted to Luma component system
- Input system adapted to Luma's input handling
- Materials and shaders converted to Luma's format
- Physics system adapted to use PhysX integration
