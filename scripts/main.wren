import "sdl" for App, Draw, Image, Input
import "random" for Random

System.print("Hello from Wren! (this goes to the terminal)")
System.print("Press Space for a new background color, Escape to quit.")

// Module-level vars must start with Uppercase so the compiler resolves them
// as variable lookups, not implicit `this.name` calls, when used in a class.
var Rng = Random.new()

class Game {
  // Wren has no field-declaration syntax at class scope, so this stands in
  // for an initializer: called once, below, before the game loop starts.
  static init() {
    __bgColor = [30, 30, 46]
    __spaceWasDown = false
    __slime = Image.load("assets/Abyss_Slime_D_Jump_1.png")
  }

  static update(dt) {
    if (Input.keyDown("Escape")) App.quit()

    // Re-roll the color once per press, not once per frame it's held.
    var spaceDown = Input.keyDown("Space")
    if (spaceDown && !__spaceWasDown) {
      __bgColor = [Rng.int(256), Rng.int(256), Rng.int(256)]
    }
    __spaceWasDown = spaceDown
  }

  static draw() {
    Draw.clear(__bgColor[0], __bgColor[1], __bgColor[2])
    Draw.color(255, 255, 255)
    var text = "Hello, Wren and SDL!"
    var textY = (App.height - 8) / 2
    Draw.text((App.width - text.count * 8) / 2, textY, text)

    // Centered just above the text, drawn at 2x size.
    var scale = 2
    var w = __slime.width * scale
    var h = __slime.height * scale
    Draw.image(__slime, (App.width - w) / 2, textY - h - 8, scale)
  }
}

Game.init()
