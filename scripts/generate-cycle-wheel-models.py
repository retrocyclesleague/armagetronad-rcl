#!/usr/bin/env python3
"""Regenerate the native cycle wheel meshes with smoother radial rings.

This is deliberately not a general vehicle generator. The dimensions, centers,
ring proportions, topology, vertex order, and face winding below are taken from
the original Armagetron `cycle_front.mod` and `cycle_rear.mod` meshes. Only the
ten-segment radial rings are increased to 32 segments.
"""

from __future__ import annotations

import math
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, Iterable, List, Sequence, Tuple


SEGMENTS = 32
REPO_ROOT = Path(__file__).resolve().parents[1]

Vec3 = Tuple[float, float, float]
Face = Tuple[int, int, int]


@dataclass(frozen=True)
class RingSpec:
    center_x: float
    center_z: float
    min_x: float
    max_x: float
    min_z: float
    max_z: float


@dataclass(frozen=True)
class WheelSpec:
    output: str
    center_x: float
    center_z: float
    outer: RingSpec
    inner: RingSpec
    center_y: float = 0.00019
    positive_y: float = 0.29717
    negative_y: float = -0.29679


# Bounds and centers are kept verbatim from the native ten-segment exports.
FRONT = WheelSpec(
    output="models/cycle_front.mod",
    center_x=0.0,
    center_z=0.0,
    outer=RingSpec(0.0, 0.0, -0.439773, 0.43977, -0.418248, 0.418248),
    inner=RingSpec(0.0, 0.0, -0.336475, 0.336475, -0.320005, 0.320007),
)

REAR = WheelSpec(
    output="models/cycle_rear.mod",
    center_x=0.0004475,
    center_z=0.0002975,
    outer=RingSpec(
        0.0004475,
        0.0002975,
        -0.7435,
        0.744397,
        -0.70724,
        0.707835,
    ),
    inner=RingSpec(
        0.0004475,
        0.0002975,
        -0.568755,
        0.569653,
        -0.541047,
        0.541643,
    ),
)


class Model:
    def __init__(self) -> None:
        self.vertices: List[Vec3] = []
        self.faces: List[Face] = []
        self.face_groups: Dict[str, List[Face]] = {}
        self.lines: List[str] = []

    def add_vertex(self, vertex: Vec3) -> int:
        self.vertices.append(vertex)
        index = len(self.vertices)
        self.lines.append(
            "v {}\t{}\t{}\t{}".format(index, *(format_number(v) for v in vertex))
        )
        return index

    def add_face(self, face: Face, group: str) -> None:
        self.faces.append(face)
        self.face_groups.setdefault(group, []).append(face)
        self.lines.append("f \t{}\t{}\t{}".format(*face))

    def render(self) -> str:
        return "\n".join(self.lines) + "\n"


def format_number(value: float) -> str:
    if abs(value) < 0.00000005:
        value = 0.0
    result = f"{value:.7f}".rstrip("0").rstrip(".")
    return "0" if result == "-0" else result


def cardinal_value(value: float) -> float:
    """Snap sin/cos near cardinal axes so original bounds remain exact."""
    if abs(value) < 1e-12:
        return 0.0
    if abs(value - 1.0) < 1e-12:
        return 1.0
    if abs(value + 1.0) < 1e-12:
        return -1.0
    return value


def scaled_axis(unit: float, center: float, minimum: float, maximum: float) -> float:
    extent = maximum - center if unit >= 0.0 else center - minimum
    return center + unit * extent


def ring_vertices(spec: RingSpec, y: float) -> Iterable[Vec3]:
    # Match the native order: start at -X and advance through +Z toward +X.
    for segment in range(SEGMENTS):
        angle = math.pi - math.tau * segment / SEGMENTS
        unit_x = cardinal_value(math.cos(angle))
        unit_z = cardinal_value(math.sin(angle))
        yield (
            scaled_axis(unit_x, spec.center_x, spec.min_x, spec.max_x),
            y,
            scaled_axis(unit_z, spec.center_z, spec.min_z, spec.max_z),
        )


def add_ring(model: Model, spec: RingSpec, y: float) -> List[int]:
    return [model.add_vertex(vertex) for vertex in ring_vertices(spec, y)]


def add_positive_side(
    model: Model, outer: Sequence[int], inner: Sequence[int], group: str
) -> None:
    for segment in range(SEGMENTS):
        following = (segment + 1) % SEGMENTS
        model.add_face(
            (outer[segment], inner[following], inner[segment]), group
        )
        model.add_face(
            (outer[segment], outer[following], inner[following]), group
        )


def add_negative_side(
    model: Model, outer: Sequence[int], inner: Sequence[int], group: str
) -> None:
    for segment in range(SEGMENTS):
        following = (segment + 1) % SEGMENTS
        model.add_face(
            (inner[segment], inner[following], outer[segment]), group
        )
        model.add_face(
            (inner[following], outer[following], outer[segment]), group
        )


def add_positive_cap(
    model: Model, ring: Sequence[int], center: int, group: str
) -> None:
    for segment in range(SEGMENTS):
        following = (segment + 1) % SEGMENTS
        model.add_face((center, ring[segment], ring[following]), group)


def add_negative_cap(
    model: Model, ring: Sequence[int], center: int, group: str
) -> None:
    for segment in range(SEGMENTS):
        following = (segment + 1) % SEGMENTS
        model.add_face((ring[following], ring[segment], center), group)


def build_front(spec: WheelSpec) -> Model:
    """Preserve the front wheel's native biconical hub topology."""
    model = Model()
    outer = add_ring(model, spec.outer, spec.center_y)
    positive = add_ring(model, spec.inner, spec.positive_y)
    positive_center = model.add_vertex(
        (spec.center_x, spec.positive_y, spec.center_z)
    )

    add_positive_side(model, outer, positive, "positive_side")
    add_positive_cap(model, positive, positive_center, "positive_cap")

    negative = add_ring(model, spec.inner, spec.negative_y)
    negative_center = model.add_vertex(
        (spec.center_x, spec.negative_y, spec.center_z)
    )

    add_negative_side(model, outer, negative, "negative_side")
    add_negative_cap(model, negative, negative_center, "negative_cap")
    return model


def build_rear(spec: WheelSpec) -> Model:
    """Preserve the rear wheel's two separately wound center-plane caps."""
    model = Model()
    middle_center = model.add_vertex(
        (spec.center_x, spec.center_y, spec.center_z)
    )
    outer = add_ring(model, spec.outer, spec.center_y)
    positive = add_ring(model, spec.inner, spec.positive_y)
    positive_center = model.add_vertex(
        (spec.center_x, spec.positive_y, spec.center_z)
    )

    for segment in range(SEGMENTS):
        following = (segment + 1) % SEGMENTS
        model.add_face(
            (middle_center, outer[following], outer[segment]),
            "positive_middle_cap",
        )
    add_positive_side(model, outer, positive, "positive_side")
    add_positive_cap(model, positive, positive_center, "positive_cap")

    negative = add_ring(model, spec.inner, spec.negative_y)
    negative_center = model.add_vertex(
        (spec.center_x, spec.negative_y, spec.center_z)
    )

    for segment in range(SEGMENTS):
        following = (segment + 1) % SEGMENTS
        model.add_face(
            (outer[segment], outer[following], middle_center),
            "negative_middle_cap",
        )
    add_negative_side(model, outer, negative, "negative_side")
    add_negative_cap(model, negative, negative_center, "negative_cap")
    return model


def subtract(a: Vec3, b: Vec3) -> Vec3:
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def cross(a: Vec3, b: Vec3) -> Vec3:
    return (
        a[1] * b[2] - a[2] * b[1],
        a[2] * b[0] - a[0] * b[2],
        a[0] * b[1] - a[1] * b[0],
    )


def face_normal(model: Model, face: Face) -> Vec3:
    a, b, c = (model.vertices[index - 1] for index in face)
    return cross(subtract(b, a), subtract(c, a))


def require(condition: bool, message: object) -> None:
    if not condition:
        raise ValueError(message)


def validate_model(
    model: Model,
    spec: WheelSpec,
    expected_vertices: int,
    expected_faces: int,
    cap_directions: Dict[str, float],
) -> None:
    require(
        len(model.vertices) == expected_vertices,
        f"expected {expected_vertices} vertices, got {len(model.vertices)}",
    )
    require(
        len(model.faces) == expected_faces,
        f"expected {expected_faces} faces, got {len(model.faces)}",
    )

    xs = [vertex[0] for vertex in model.vertices]
    ys = [vertex[1] for vertex in model.vertices]
    zs = [vertex[2] for vertex in model.vertices]
    actual_bounds = (min(xs), max(xs), min(ys), max(ys), min(zs), max(zs))
    expected_bounds = (
        spec.outer.min_x,
        spec.outer.max_x,
        spec.negative_y,
        spec.positive_y,
        spec.outer.min_z,
        spec.outer.max_z,
    )
    require(actual_bounds == expected_bounds, (actual_bounds, expected_bounds))

    for face in model.faces:
        require(
            min(face) >= 1 and max(face) <= len(model.vertices),
            ("face index out of bounds", face),
        )
        normal = face_normal(model, face)
        magnitude_squared = sum(component * component for component in normal)
        require(magnitude_squared > 1e-18, ("degenerate face", face))

    for group, direction in cap_directions.items():
        for face in model.face_groups[group]:
            normal = face_normal(model, face)
            require(normal[1] * direction > 0.0, (group, face, normal))

    for group in ("positive_side", "negative_side"):
        for face in model.face_groups[group]:
            normal = face_normal(model, face)
            centroid_x = sum(model.vertices[index - 1][0] for index in face) / 3.0
            centroid_z = sum(model.vertices[index - 1][2] for index in face) / 3.0
            radial_x = centroid_x - spec.center_x
            radial_z = centroid_z - spec.center_z
            require(
                normal[0] * radial_x + normal[2] * radial_z > 0.0,
                (group, face, normal),
            )


def write_model(spec: WheelSpec, model: Model) -> None:
    output = REPO_ROOT / spec.output
    with output.open("w", encoding="ascii", newline="\n") as stream:
        stream.write(model.render())
    print(
        f"generated {spec.output}: "
        f"{len(model.vertices)} vertices, {len(model.faces)} faces"
    )


def main() -> None:
    front = build_front(FRONT)
    validate_model(
        front,
        FRONT,
        expected_vertices=3 * SEGMENTS + 2,
        expected_faces=6 * SEGMENTS,
        cap_directions={"positive_cap": 1.0, "negative_cap": -1.0},
    )

    rear = build_rear(REAR)
    validate_model(
        rear,
        REAR,
        expected_vertices=3 * SEGMENTS + 3,
        expected_faces=8 * SEGMENTS,
        cap_directions={
            "positive_middle_cap": -1.0,
            "positive_cap": 1.0,
            "negative_middle_cap": 1.0,
            "negative_cap": -1.0,
        },
    )

    write_model(FRONT, front)
    write_model(REAR, rear)


if __name__ == "__main__":
    main()
