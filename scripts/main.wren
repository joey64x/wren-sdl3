import "sdl" for App, Draw, Image, Input
import "random" for Random
import "animation" for Animation, SpriteSheet

System.print("Hello from Wren! (this goes to the terminal)")
System.print("Press Space to jump and change the background color, Escape to quit.")

// Module-level vars must start with Uppercase so the compiler resolves them
// as variable lookups, not implicit `this.name` calls, when used in a class.
var Rng = Random.new()

// How high the jump arc peaks, in unscaled sprite pixels.
var JumpHeight = 24

class Game {
  // Wren has no field-declaration syntax at class scope, so this stands in
  // for an initializer: called once, below, before the game loop starts.
  static init() {
    __bgColor = [30, 30, 46]
    __spaceWasDown = false
    // 6 frames, played at 8 frames per second.
    __idle = Animation.strip(Image.load("assets/Abyss_Slime_D_Idle.png"), 6, 8)

    // Row 1 of the sheet holds the same 3-frame hop twice; play it once.
    var sheet = SpriteSheet.new(Image.load("assets/Abyss_Slime_D.png"), 64, 64)
    __jump = Animation.new(sheet, 1, 3, 8)
    __jump.looping = false
    __jumping = false
    __height = 0  // how far the slime is off the ground, in sprite pixels
  }

  static update(dt) {
    if (Input.keyDown("Escape")) App.quit()

    // Act once per press, not once per frame it's held. A press mid-jump
    // still changes the color but doesn't restart the jump.
    var spaceDown = Input.keyDown("Space")
    if (spaceDown && !__spaceWasDown) {
      __bgColor = [Rng.int(256), Rng.int(256), Rng.int(256)]
      if (!__jumping) {
        __jumping = true
        __jump.restart()
      }
    }
    __spaceWasDown = spaceDown

    if (__jumping) {
      __jump.update(dt)
      if (__jump.finished) __jumping = false
    } else {
      __idle.update(dt)
    }

    // A parabola over the jump: 0 at the start and end, JumpHeight midway.
    if (__jumping) {
      var t = __jump.progress
      __height = JumpHeight * 4 * t * (1 - t)
    } else {
      __height = 0
    }
  }

  static draw() {
    Draw.clear(__bgColor[0], __bgColor[1], __bgColor[2])
    Draw.color(255, 255, 255)
    var text = "Hello, Wren and SDL!"
    var textY = (App.height - 8) / 2
    Draw.text((App.width - text.count * 8) / 2, textY, text)

    // Centered just above the text, drawn at 2x size. The idle and jump
    // frames differ in height but both have the slime at the bottom edge,
    // so line their bottoms up on the ground line instead of their tops.
    var scale = 2
    var ground = textY - 8
    var anim = __jumping ? __jump : __idle
    var w = anim.frameWidth * scale
    var h = anim.frameHeight * scale
    var y = ground - h - __height * scale
    anim.draw((App.width - w) / 2, y, scale)
  }
}

Game.init()
