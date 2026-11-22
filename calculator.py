"""
Calculator utilities.
"""

import math
from typing import Union

Number = Union[int, float]


def circle_area(radius: Number) -> float:
    """Return area of a circle with given radius using πr².

    Parameters:
        radius: Circle radius.

    Returns:
        Area as float.
    """
    r = float(radius)
    return math.pi * r * r


__all__ = ["circle_area"]
