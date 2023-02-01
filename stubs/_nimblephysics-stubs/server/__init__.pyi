"""This provides a native WebSocket server infrastructure."""
from __future__ import annotations
import nimblephysics_libs._nimblephysics.server
import typing
import nimblephysics_libs._nimblephysics.dynamics
import nimblephysics_libs._nimblephysics.simulation
import numpy
_Shape = typing.Tuple[int, ...]

__all__ = [
    "GUIRecording",
    "GUIStateMachine",
    "GUIWebsocketServer"
]


class GUIStateMachine():
    def __init__(self) -> None: ...
    def clear(self) -> None: ...
    def clearBodyWrench(self, body: nimblephysics_libs._nimblephysics.dynamics.BodyNode, prefix: str = 'wrench') -> None: ...
    def createBox(self, key: str, size: numpy.ndarray[numpy.float64, _Shape[3, 1]] = array([1., 1., 1.]), pos: numpy.ndarray[numpy.float64, _Shape[3, 1]] = array([0., 0., 0.]), euler: numpy.ndarray[numpy.float64, _Shape[3, 1]] = array([0., 0., 0.]), color: numpy.ndarray[numpy.float64, _Shape[4, 1]] = array([0.5, 0.5, 0.5, 1. ]), layer: str = '', castShadows: bool = True, receiveShadows: bool = False) -> None: ...
    def createButton(self, key: str, label: str, fromTopLeft: numpy.ndarray[numpy.int32, _Shape[2, 1]], size: numpy.ndarray[numpy.int32, _Shape[2, 1]], onClick: typing.Callable[[], None], layer: str = '') -> None: ...
    def createLayer(self, key: str, color: numpy.ndarray[numpy.float64, _Shape[4, 1]] = array([0.5, 0.5, 0.5, 1. ]), defaultShow: bool = True) -> None: ...
    def createLine(self, key: str, points: typing.List[numpy.ndarray[numpy.float64, _Shape[3, 1]]], color: numpy.ndarray[numpy.float64, _Shape[4, 1]] = array([0.5, 0.5, 0.5, 1. ]), layer: str = '') -> None: ...
    def createMeshFromShape(self, key: str, mesh: nimblephysics_libs._nimblephysics.dynamics.MeshShape, pos: numpy.ndarray[numpy.float64, _Shape[3, 1]] = array([0., 0., 0.]), euler: numpy.ndarray[numpy.float64, _Shape[3, 1]] = array([0., 0., 0.]), scale: numpy.ndarray[numpy.float64, _Shape[3, 1]] = array([1., 1., 1.]), color: numpy.ndarray[numpy.float64, _Shape[4, 1]] = array([0.5, 0.5, 0.5, 1. ]), layer: str = '', castShadows: bool = True, receiveShadows: bool = False) -> None: ...
    def createPlot(self, key: str, fromTopLeft: numpy.ndarray[numpy.int32, _Shape[2, 1]], size: numpy.ndarray[numpy.int32, _Shape[2, 1]], xs: typing.List[float], minX: float, maxX: float, ys: typing.List[float], minY: float, maxY: float, plotType: str, layer: str = '') -> None: ...
    def createRichPlot(self, key: str, fromTopLeft: numpy.ndarray[numpy.int32, _Shape[2, 1]], size: numpy.ndarray[numpy.int32, _Shape[2, 1]], minX: float, maxX: float, minY: float, maxY: float, title: str, xAxisLabel: str, yAxisLabel: str, layer: str = '') -> None: ...
    def createSlider(self, key: str, fromTopLeft: numpy.ndarray[numpy.int32, _Shape[2, 1]], size: numpy.ndarray[numpy.int32, _Shape[2, 1]], min: float, max: float, value: float, onlyInts: bool, horizontal: bool, onChange: typing.Callable[[float], None], layer: str = '') -> None: ...
    def createSphere(self, key: str, radii: numpy.ndarray[numpy.float64, _Shape[3, 1]] = array([0.5, 0.5, 0.5]), pos: numpy.ndarray[numpy.float64, _Shape[3, 1]] = array([0., 0., 0.]), color: numpy.ndarray[numpy.float64, _Shape[4, 1]] = array([0.5, 0.5, 0.5, 1. ]), layer: str = '', castShadows: bool = True, receiveShadows: bool = False) -> None: ...
    def createText(self, key: str, contents: str, fromTopLeft: numpy.ndarray[numpy.int32, _Shape[2, 1]], size: numpy.ndarray[numpy.int32, _Shape[2, 1]], layer: str = '') -> None: ...
    def deleteObject(self, key: str) -> None: ...
    def deleteUIElement(self, key: str) -> None: ...
    def getObjectColor(self, key: str) -> numpy.ndarray[numpy.float64, _Shape[4, 1]]: ...
    def getObjectPosition(self, key: str) -> numpy.ndarray[numpy.float64, _Shape[3, 1]]: ...
    def getObjectRotation(self, key: str) -> numpy.ndarray[numpy.float64, _Shape[3, 1]]: ...
    def renderBasis(self, scale: float = 10.0, prefix: str = 'basis', pos: numpy.ndarray[numpy.float64, _Shape[3, 1]] = array([0., 0., 0.]), euler: numpy.ndarray[numpy.float64, _Shape[3, 1]] = array([0., 0., 0.]), layer: str = '') -> None: ...
    def renderBodyWrench(self, body: nimblephysics_libs._nimblephysics.dynamics.BodyNode, wrench: numpy.ndarray[numpy.float64, _Shape[6, 1]], scaleFactor: float = 0.1, prefix: str = 'wrench', layer: str = '') -> None: ...
    def renderMovingBodyNodeVertices(self, body: nimblephysics_libs._nimblephysics.dynamics.BodyNode, scaleFactor: float = 0.1, prefix: str = 'vert-vel', layer: str = '') -> None: ...
    def renderSkeleton(self, skeleton: nimblephysics_libs._nimblephysics.dynamics.Skeleton, prefix: str = 'world', overrideColor: numpy.ndarray[numpy.float64, _Shape[4, 1]] = array([-1., -1., -1., -1.]), layer: str = '') -> None: ...
    def renderTrajectoryLines(self, world: nimblephysics_libs._nimblephysics.simulation.World, positions: numpy.ndarray[numpy.float64, _Shape[m, n]], prefix: str = 'trajectory', layer: str = '') -> None: ...
    def renderWorld(self, world: nimblephysics_libs._nimblephysics.simulation.World, prefix: str = 'world', renderForces: bool = True, renderForceMagnitudes: bool = True, layer: str = '') -> None: ...
    def setButtonLabel(self, key: str, label: str) -> None: ...
    def setFramesPerSecond(self, framesPerSecond: int) -> None: ...
    def setObjectColor(self, key: str, color: numpy.ndarray[numpy.float64, _Shape[4, 1]]) -> None: ...
    def setObjectPosition(self, key: str, position: numpy.ndarray[numpy.float64, _Shape[3, 1]]) -> None: ...
    def setObjectRotation(self, key: str, euler: numpy.ndarray[numpy.float64, _Shape[3, 1]]) -> None: ...
    def setObjectScale(self, key: str, scale: numpy.ndarray[numpy.float64, _Shape[3, 1]]) -> None: ...
    def setObjectTooltip(self, key: str, tooltip: str) -> None: ...
    def setPlotData(self, key: str, xs: typing.List[float], minX: float, maxX: float, ys: typing.List[float], minY: float, maxY: float) -> None: ...
    def setRichPlotBounds(self, key: str, minX: float, maxX: float, minY: float, maxY: float) -> None: ...
    def setRichPlotData(self, key: str, name: str, color: str, plotType: str, xs: typing.List[float], ys: typing.List[float]) -> None: ...
    def setSliderMax(self, key: str, value: float) -> None: ...
    def setSliderMin(self, key: str, value: float) -> None: ...
    def setSliderValue(self, key: str, value: float) -> None: ...
    def setTextContents(self, key: str, contents: str) -> None: ...
    def setUIElementPosition(self, key: str, position: numpy.ndarray[numpy.int32, _Shape[2, 1]]) -> None: ...
    def setUIElementSize(self, key: str, size: numpy.ndarray[numpy.int32, _Shape[2, 1]]) -> None: ...
    pass
class GUIRecording(GUIStateMachine):
    def __init__(self) -> None: ...
    def getFrameJson(self, frame: int) -> str: ...
    def getFramesJson(self, startFrame: int = 0) -> str: ...
    def getNumFrames(self) -> int: ...
    def saveFrame(self) -> None: ...
    def writeFrameJson(self, path: str, frame: int) -> None: ...
    def writeFramesJson(self, path: str, startFrame: int = 0) -> None: ...
    pass
class GUIWebsocketServer(GUIStateMachine):
    def __init__(self) -> None: ...
    def blockWhileServing(self) -> None: ...
    def clear(self) -> None: ...
    def flush(self) -> None: ...
    def getKeysDown(self) -> typing.Set[str]: ...
    def getScreenSize(self) -> numpy.ndarray[numpy.int32, _Shape[2, 1]]: ...
    def isKeyDown(self, key: str) -> bool: ...
    def isServing(self) -> bool: ...
    def registerConnectionListener(self, listener: typing.Callable[[], None]) -> None: ...
    def registerDragListener(self, key: str, listener: typing.Callable[[numpy.ndarray[numpy.float64, _Shape[3, 1]]], None], endDrag: typing.Callable[[], None]) -> GUIWebsocketServer: ...
    def registerKeydownListener(self, listener: typing.Callable[[str], None]) -> None: ...
    def registerKeyupListener(self, listener: typing.Callable[[str], None]) -> None: ...
    def registerScreenResizeListener(self, listener: typing.Callable[[numpy.ndarray[numpy.int32, _Shape[2, 1]]], None]) -> None: ...
    def registerShutdownListener(self, listener: typing.Callable[[], None]) -> None: ...
    def registerTooltipChangeListener(self, key: str, listener: typing.Callable[[str], None]) -> GUIWebsocketServer: ...
    def serve(self, port: int) -> None: ...
    def stopServing(self) -> None: ...
    pass