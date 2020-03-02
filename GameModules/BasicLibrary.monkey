Import mojo

Class CollisionBox
	Field x:Float
	Field y:Float
	Field width:Int
	Field height:Int
	Field type:String
	
	Method New(_type:String,_x:Float,_y:Float,_width:Int,_height:Int)
		x = _x
		y = _y
		type = _type
		width = _width
		height = _height
	End Method
	
	Method IntersectsBox:Bool(Box:CollisionBox)
		If (x < Box.GetX() + Box.GetWidth()) And (x + width > Box.GetX()) And (y < Box.GetY() + Box.GetHeight()) And (y + h > Box.GetY()) Then
			Return True
		Else 
			Return False
		End If 
	End Method
	
	Method UpdateX(_x:Float)
		x = _x
	End Method
	
	Method UpdateY(_y:Float)
		y = _y
	End Method
	
	Method GetX:Float()
		Return x 
	End Method
	
	Method GetY:Float()
		Return y
	End Method
	
	Method GetWidth:Int()
		Return width
	End Method
	
	Method GetHeight:Int()
		Return height
	End Method
	
	
End Class

Class GameObject
	Field x:Float
	Field y:Float
	Field width:Int
	Field height:Int
	Field path:String
	Field sprite:Image
	
	Method New(_x:Float,_y:Float,_width:Int,_height:Int,_path:String)
		x = _x
		y = _y
		width = _width
		height = _height
		path = _path
		sprite = LoadImage(path,width,height)
	End Method
	
End Class

Class Crosshair Extends GameObject
	
	Method New(type:Int)
		sprite = LoadImage("gui/xhairs/" + type + ".png")
	End Method
	
	Method Draw()
		DrawImage(sprite,MouseX()-7,MouseY()-7)
	End Method
End Class

Class Vec2D
	Field VecX:Float
	Field VecY:Float
	Field magnitude:Float
	
	Method New(x:Float, y:Float)
		VecX = x
		VecY = y
	End

	Method UpdateX(x:Float)
		VecX = x
	End
	
	Method UpdateY(y:Float)
		VecY = y
	End
	
	Method GetMagnitude:Float()
		Return magnitude
	End

	
	Method GetX:Float()
		Return VecX
	End
	
	Method GetY:Float()
		Return VecY
	End
	
	Method Reset()
		VecX = 0
		VecY = 0
	End
	
	Method Normalise()
		magnitude = Sqrt( ( (VecX * VecX) + (VecY * VecY)))
		VecX /= magnitude
		VecY /= magnitude
	End
End

Class Animation Extends GameObject
	Field spriteNum:Int
	Field frameTime:Int
	Field xScale:Float
	Field yScale:Float
	
	Method New(_path:String, _width:Int, _height:Int, _spriteNum:Int, _frameTime:Int, _xScale:Float, _yScale:Float)
		spriteNum = _spriteNum
		frameTime = _frameTime
		width = _width
		height = _height
		path = _path
		xScale = _xScale
		yScale = _yScale
		sprite = LoadImage(path, width, height, spriteNum)
	End Method
	
	Method Play(x:Float, y:Float)
		Local frameCounter = (Millisecs() Mod (spriteNum * frameTime)) / frameTime
		DrawImage(sprite, x, y, 0, xScale, yScale, frameCounter)
	End Method
	
	Method Play(x:Float, y:Float, rotation:Int)
		Local frameCounter = (Millisecs() Mod (spriteNum * frameTime)) / frameTime
		DrawImage(sprite, x, y, rotation, xScale, yScale, frameCounter)
	End Method
	
End Class

Function findDist:Int(x1:Float,y1:Float,x2:Float,y2:Float)
	Local xDist:Float = x1-x2
	Local yDist:Float = y1-y2
	Return Sqrt((xDist*xDist)+(yDist*yDist))
End Function

Function Collide:Bool(x:Int,y:Int,w:Int,h:Int,x1:Int,y1:Int,w1:Int,h1:Int)
	If (x < x1 + w1) And (x + w > x1) And (y < y1 + h1) And (y + h > y1) Then
		Return True
	Else 
		Return False
	End If 
End Function