import math
from calculator import circle_area


def test_circle_area_unit():
    assert math.isclose(circle_area(1), math.pi, rel_tol=0, abs_tol=1e-12)


def test_circle_area_zero():
    assert circle_area(0) == 0.0


def test_circle_area_two():
    assert math.isclose(circle_area(2), 4 * math.pi, rel_tol=0, abs_tol=1e-12)
