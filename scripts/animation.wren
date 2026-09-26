import "sdl" for Draw

// An Image cut into a grid of equal-sized frames. Column 0, row 0 is the
// top-left frame.
class SpriteSheet {
  construct new(image, frameWidth, frameHeight) {
    _image = image
    _frameWidth = frameWidth
    _frameHeight = frameHeight
  }

  image { _image }
  frameWidth { _frameWidth }
  frameHeight { _frameHeight }
  columns { (_image.width / _frameWidth).floor }
  rows { (_image.height / _frameHeight).floor }

  draw(column, row, x, y) { draw(column, row, x, y, 1) }

  draw(column, row, x, y, scale) {
    Draw.imageRect(_image, column * _frameWidth, row * _frameHeight,
                   _frameWidth, _frameHeight, x, y, scale)
  }
}

// An animation: the first frameCount frames of one row of a SpriteSheet,
// played left to right. Call update(dt) every frame, then draw(x, y).
// It loops by default; set looping = false to play it once and hold the last
// frame, and restart() to play it again.
class Animation {
  construct new(sheet, row, frameCount, fps) {
    _sheet = sheet
    _row = row
    _frameCount = frameCount
    _frameTime = 1 / fps
    _time = 0
    _looping = true
  }

  // For an Image that is a single row of frameCount equal-width frames.
  construct strip(image, frameCount, fps) {
    _sheet = SpriteSheet.new(image, image.width / frameCount, image.height)
    _row = 0
    _frameCount = frameCount
    _frameTime = 1 / fps
    _time = 0
    _looping = true
  }

  sheet { _sheet }
  frameWidth { _sheet.frameWidth }
  frameHeight { _sheet.frameHeight }

  looping { _looping }
  looping=(value) { _looping = value }

  // Seconds for one pass through all the frames.
  duration { _frameCount * _frameTime }

  // How far through the current pass, from 0 to 1.
  progress { _time / duration }

  // True once a non-looping animation has shown its last frame.
  finished { !_looping && _time >= duration }

  // Index of the frame being shown, 0 to frameCount - 1.
  frame { (_time / _frameTime).floor.min(_frameCount - 1) }

  restart() { _time = 0 }

  update(dt) {
    _time = _time + dt
    if (_looping) {
      // Wrap so the time doesn't grow forever and lose precision.
      _time = _time % duration
    } else {
      _time = _time.min(duration)
    }
  }

  draw(x, y) { draw(x, y, 1) }
  draw(x, y, scale) { _sheet.draw(frame, _row, x, y, scale) }
}
