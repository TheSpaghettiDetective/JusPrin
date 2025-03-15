// Basic geometry types

export class Vec2d {
    constructor(public x: number = 0, public y: number = 0) {}

    clone(): Vec2d {
        return new Vec2d(this.x, this.y);
    }

    toString(): string {
        return `${this.x},${this.y}`;
    }
}

export class Vec3d {
    constructor(public x: number = 0, public y: number = 0, public z: number = 0) {}

    clone(): Vec3d {
        return new Vec3d(this.x, this.y, this.z);
    }

    toString(): string {
        return `${this.x},${this.y},${this.z}`;
    }
}