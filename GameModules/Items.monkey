Import mojo
Import BasicLibrary
Import Enemies

Interface Interactables
	Method Use(player:Player)
	Method Drop()
	Method PickUp()
	Method Draw()
	Method Draw(pos:Int)
	Method GetX:Float()
	Method GetY:Float()
End

Interface WeaponInterface
	Method Drop()
	Method PickUp()
	Method Attack(px:Int, py:Int, target:List<Enemy>)
	Method GetX:Int()
	Method GetY:Int()
	Method DrawItem()
	Method Draw(x:Int, y:Int)
End 

Class Item Extends GameObject
	Field id:Int
	Field range:Int = 30
	Field state:String
	Field guiImage:Image
	
	Method New(_x:Float,_y:Float,_id:Int)
		x = _x
		y = _y
		id = _id
		state = "Dropped"
		guiImage = LoadImage("gui/items/" + id + ".png",1)
	End
	
		
	Method PickUpItem()
		state = "Equipped"
	End 
		
	Method DrawItem(pos:int)
		If state = "Dropped"
			'use id number to draw correct image
			DrawImage(guiImage,x,y)
		End If
		If state = "Equipped"
			DrawImage(guiImage,548 + (48*pos),442)
		End If
	End Method
	
	Method DropItem()
		state = "Dropped"
	End
	
	Method setPos(_x:Float,_y:Float)
		x = _x
		y = _y
	End Method
	
	Method GetState:String()
		Return state
	End Method
	
End Class

Class HealthPotion Extends Item Implements Interactables
	
	Method New(_x:Float, _y:Float)
		Super.New(_x, _y, 2)
	End
	
	Method Use(player:Player)
		player.Heal()
	End Method
	
	Method PickUp()
		Super.PickUpItem()
	End
	
	Method Drop()
		Super.DropItem
	End
	
	Method Draw()
		Super.DrawItem(0)
	End
	
	Method Draw(pos:Int)
		Super.DrawItem(pos)
	End
	
	Method GetX:Float()
		Return x
	End
	
	Method GetY:Float()
		Return y
	End
	
End Class

Class Weapon Extends GameObject
	Field id:Int
	Field range:Int = 30
	Field state:String
	Field guiImage:Image
	Field damage:Int
	Field cd:Int = 250
	Field cdTime:Int = 0
	
End Class

Class Sword Extends Weapon Implements WeaponInterface
	Field angle:Int = 0
	Field angleX:Int = 0
	Field angleY:Int = 0
	Field targetAngle:Int = 0
	Field update:Bool = True
	Field hitbox:String = "NONE"
	Field damageWidth:Int = 64
	Field damageHeight:Int = 32
	Field slashSound:Sound
	Field slashRight:Animation
	Field slashLeft:Animation
	Field slashUp:Animation
	Field slashDown:Animation
	
	Method New(_damage:Int, _path:String, _width:Int, _height:Int,_id:Int)
		width = _width
		height = _height
		path = _path
		damage = _damage
		sprite = LoadImage(path, width, height, 1)
		state = "Dropped"
		id = _id
		guiImage = LoadImage("gui/items/" + id + ".png",1)
		x = 320
		y = 240
		slashSound = LoadSound("soundfx/sword.mp3")
		slashRight = New Animation("effects/slashRight.png",32,64,3,75,1,1)
		slashLeft = New Animation("effects/slashLeft.png",32,64,3,75,1,1)
		slashUp = New Animation("effects/slashUp.png",64,32,3,75,1,1)
		slashDown = New Animation("effects/slashDown.png",64,32,3,75,1,1)
	End Method
	
	Method Attack(px:Int,py:Int,target:List<Enemy>)
		If MouseHit(MOUSE_LEFT) And (Millisecs >= cdTime) Then
			update = False
			targetAngle = angle - 270
			cdTime = Millisecs() + cd
			PlaySound(slashSound,1,0)
			For Local i:= Eachin target
				If (angle < -45 And angle > -90) Or (angle < 270 And angle > 235) Then
					i.Damage(px-((damageWidth-10)/2)+6,py-(damageHeight)+18,damageWidth,damageHeight)'up
					hitbox = "UP"
				Else If angle < 235 And angle > 125
					i.Damage(px+28,py-((damageWidth-30)/2)+16,damageHeight,damageWidth)	'down
					hitbox = "RIGHT"
				Else If angle < 125 And angle > 45
					i.Damage(px-((damageWidth-10)/2)+6,py+48,damageWidth,damageHeight)'left
					hitbox = "DOWN"
				Else If angle <45 And angle > -45
					i.Damage(px-(damageHeight)+6,py-((damageWidth-30)/2)+16,damageHeight,damageWidth)'right
					hitbox = "LEFT"
				End If 
			Next
			
			If (angle < -45 And angle > -90) Or (angle < 270 And angle > 235) Then
				hitbox = "UP"
			Else If angle < 235 And angle > 125
				hitbox = "RIGHT"
			Else If angle < 125 And angle > 45
				hitbox = "DOWN"
			Else If angle <45 And angle > -45
				hitbox = "LEFT"
			End If 
				
		Endif
		
		If angle <= targetAngle
			update = True
		EndIf
		
		If update = True Then
			angleX = MouseX() - (px + 14)
			angleY = MouseY() - (py + 25)
			angle = ATan2(angleX, angleY) + 90
		Else
			angle -= 30
		End If
	End Method
	
	Method DrawItem()
		If state = "Dropped"
			'use id number to draw correct image
			DrawImage(guiImage,x,y)
		End If
	End
	
	Method Draw(_x:Int, _y:Int)
		'+14 x +30 y centre of player
		'0 x=14 y=30
		'45 x=12 y=35
		'90 x=10 y=40
		If state = "Equipped"
			DrawImage(guiImage, 500, 442)
			Local yOff = (0 * Cos(angle)) - (-5 * Sin(angle))
			Local xOff = (-5 * Cos(angle)) + (0 * Sin(angle))
			DrawImage(sprite, _x + 10 + xOff, _y + 30 + yOff, angle, 1.25, 1.25)
			If Millisecs <= cdTime
			Select hitbox
				Case "UP"
					slashUp.Play(_x-((damageWidth-10)/2)+6,_y-(damageHeight)+18)
					'DrawRect(_x-((damageWidth-10)/2)+6,_y-(damageHeight)+18,damageWidth,damageHeight)
				Case "LEFT"
					slashLeft.Play(_x-(damageHeight)+6,_y-((damageWidth-30)/2)+16)
					'DrawRect(_x-(damageHeight)+6,_y-((damageWidth-30)/2)+16,damageHeight,damageWidth)
				Case "DOWN"
					slashDown.Play(_x-((damageWidth-10)/2)+6,_y+48)
					'DrawRect(_x-((damageWidth-10)/2)+6,_y+48,damageWidth,damageHeight)
				Case "RIGHT"
					slashRight.Play(_x+28,_y-((damageWidth-30)/2)+16)
					'DrawRect(_x+28,_y-((damageWidth-30)/2)+16,damageHeight,damageWidth)
			End Select
			End If 
		End If
	End Method
	
	Method GetX:Int()
		Return x
	End Method

	Method GetY:Int()
		Return y
	End Method
	
	Method PickUp()
		state = "Equipped"
	End
	
	Method Drop()
		state = "Dropped"
	End
	
End Class