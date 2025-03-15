import { Vec2d, Vec3d } from '../src/geometry';

// Test helper functions and mock data
export function createVec2d(x: number = 0, y: number = 0): Vec2d {
    return new Vec2d(x, y);
}

export function createVec3d(x: number = 0, y: number = 0, z: number = 0): Vec3d {
    return new Vec3d(x, y, z);
}

// Add more test helpers as needed...