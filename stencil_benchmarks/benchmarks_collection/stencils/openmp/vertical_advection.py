from .mixin import VerticalAdvectionMixin
from ..base import VerticalAdvectionStencil


class KInnermost(VerticalAdvectionMixin, VerticalAdvectionStencil):
    pass


class KMiddle(VerticalAdvectionMixin, VerticalAdvectionStencil):
    pass