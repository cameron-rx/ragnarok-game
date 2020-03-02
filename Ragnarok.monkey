
Import mojo
Import GameModules.LevelGeneration
Import GameModules.BasicLibrary
Import GameModules.Enemies
Import GameModules.Items
Import GameModules.UserPlayer
Global game:Game


Function Main()
	game = New Game	
End Function

Class Game Extends App
	'Menu Variables
	Field gameState:String = "MENU"
	Field menuBackground:Image
	Field menuBut:Button 
	Field optionsBut:Button
	Field exitBut:Button
	Field playBut:Button
	Field menuArray:Button[]
	Field optionsArray:Button[]
	Field optionsSliders:VolumeSlider[]
	Field deadArray:Button[]
	Field effectsVol:VolumeSlider
	Field musicVol:VolumeSlider
	Field butIndex:Int = 0
	Field optIndex:Int = 0
	Field xhair:Crosshair
	'Playing variables
	Field p1:Player
	'Level variables
	Field Room1:RoomMap
	Field Room2:RoomMap
	Field Room3:RoomMap
	Field currentRoom:RoomMap
	
	Method OnCreate()
		'Menu Create
		SetDeviceWindow(640, 480, 1)
		SetUpdateRate(60)
		SetMusicVolume(0.5)
		menuBackground = LoadImage("menus/mainMenu.png")
		menuBut = New Button("menus/menuButton.png")
		menuBut.SetAction("MENU")
		optionsBut = New Button("menus/optionsButton.png")
		optionsBut.SetAction("OPTIONS")
		exitBut = New Button("menus/exitButton.png")
		exitBut.SetAction("EXIT")
		playBut = New Button("menus/playButton.png")
		playBut.SetAction("PLAY")
		menuArray = [playBut,optionsBut,exitBut]
		optionsArray =[menuBut]
		deadArray =[menuBut]
		effectsVol = New VolumeSlider(10,300,225,10,30)
		musicVol = New VolumeSlider(10,300,300,10,30)
		optionsSliders = [effectsVol,musicVol]
		PlayMusic("menus/menuMusic.wav", 1)
		'Playing Create
		xhair = New Crosshair(0)
		p1 = New Player(200, 200, 3, 16, 24)
		'Levels
		Room1 = New RoomMap
		Room2 = New RoomMap
		Room3 = New RoomMap
	End Method

	Method OnUpdate()
		Select gameState
			Case "MENU"
				menuArray[butIndex].SetActive(True)
				If KeyHit(KEY_S) And butIndex < 2 Then
					menuArray[butIndex].SetActive(False)
					butIndex += 1
					menuArray[butIndex].SetActive(True)
				End If
				If KeyHit(KEY_W) And butIndex > 0 Then
					menuArray[butIndex].SetActive(False)
					butIndex -= 1
					menuArray[butIndex].SetActive(True)
				End If
				If KeyHit(KEY_SPACE) Then
					If menuArray[butIndex].GetAction = "PLAY" Then
						p1.Reset()
						StopMusic()
						PlayMusic("music/level1.mp3", 1)
						Seed = Millisecs()
						Room1.Build()
						Room2.Build()
						Room3.Build()
					End If 
					gameState = menuArray[butIndex].GetAction
					menuArray[butIndex].SetActive(False)
					butIndex = 0
				End If
			Case "OPTIONS"
				Select optIndex 
					Case 0
						If KeyHit(KEY_S) optIndex += 1
						effectsVol.updateVol()
					Case 1
						If KeyHit(KEY_W) optIndex -= 1
						If KeyHit(KEY_S) optIndex += 1
						musicVol.updateVol()
					Case 2
						optionsArray[butIndex].SetActive(True)
						If KeyHit(KEY_SPACE) Then
							optionsArray[butIndex].SetActive(False)
							gameState = optionsArray[butIndex].GetAction
						End If
						If KeyHit(KEY_W) Then 
							optIndex -= 1
							optionsArray[butIndex].SetActive(False)
						End If
				End Select
				SetMusicVolume(musicVol.getVol())
			Case "PLAY"
				HideMouse()
				Room1.UpdateCurrentRoom(p1)
				p1.Update()
				p1.UpdateX()
				p1.UseItem1(p1)
				If Room1.TileCollision((p1.GetX), Floor(p1.GetY)) Then p1.CollRespX()
				p1.UpdateY()
				If Room1.TileCollision((p1.GetX), Floor(p1.GetY)) Then p1.CollRespY()
				
				If Room1.DamageCollision((p1.GetX), Floor(p1.GetY)) Then p1.Hurt()
				
				If p1.GetX <= 0 Then
					Room1.MoveRoom("EAST")
					p1.SetX(400)
				End If
				If p1.GetX >= 440 Then
					Room1.MoveRoom("WEST")
					p1.SetX(50)
				End If
				If p1.GetY <= 0 Then
					Room1.MoveRoom("NORTH")
					p1.SetY(400)
				End If
				If p1.GetY >= 440 Then
					Room1.MoveRoom("SOUTH")
					p1.SetY(50)
				End If
				If KeyHit(KEY_F)
					Room1.InteractCurrentRoom(p1)
				End If
				p1.Attack(Room1.GetCurrentRoomEnemies)
				If KeyHit(KEY_ESCAPE) Then
					Room1.Reset()
					Room2.Reset()
					Room3.Reset()
					butIndex = 0
					gameState = "MENU"
				End If
				
				If p1.GetCurHealth() = 0 Then 	
					butIndex = 0
					StopMusic()
					gameState = "DEAD"
				End If 
			Case "DEAD"
				deadArray[butIndex].SetActive(True)
				If KeyHit(KEY_SPACE) Then
					deadArray[butIndex].SetActive(False)
					gameState = deadArray[butIndex].GetAction
					PlayMusic("menus/menuMusic.wav", 1)
				End If 
				
			Case "EXIT"
				EndApp()
		End Select
	End Method

	Method OnRender()
		Cls()
		Select gameState
			Case "MENU"
				DrawImage(menuBackground,0,0)
				playBut.Draw(160,225)
				optionsBut.Draw(160,300)
				exitBut.Draw(160,375)
			Case "OPTIONS"
				DrawImage(menuBackground,0,0)
				effectsVol.Draw()
				musicVol.Draw()
				menuBut.Draw(160,375)
			Case "PLAY"	
				'menuBut.Draw(160, 375)
				Room1.DrawRoomFloor
				p1.Draw()

				Room1.DrawRoomWalls
				Room1.DrawMap()
				
				DrawText("Press ESC to reset", 500, 255)
				DrawText("Seed:", 500, 300)
				DrawText(Seed, 545, 300)
				xhair.Draw()
			Case "DEAD"
				menuBut.Draw(160, 375)
		End Select
	End Method

End Class


'####################################################################################################
'############################################MENU CLASSES############################################
'####################################################################################################
Class Button Extends GameObject
	Field active:Bool = False
	Field action:String
	
	Method New(_path:String)
		width = 320
		height = 50
		path = _path
		sprite = LoadImage(path)
	End Method
	
	Method SetAction(_action:String)
		action = _action
	End Method
	
	Method SetActive(_active:Bool)
		active = _active
	End Method
	
	Method GetAction:String()
		Return action
	End Method
	
	Method Draw(_x:Int,_y:Int)
		If active = True Then
			SetColor(255,255,255)
			DrawRect(_x-5,_y-5,width+10,height+10)
		End If
		DrawImage(sprite,_x,_y)
	End Method
	
End Class


Class VolumeSlider Extends GameObject
	Field dialNum:Int
	Field volPercent:Float
	
		Method New(_dialNum:Int,_x,_y,_width,_height)
			dialNum = _dialNum
			x = _x
			y = _y
			width = _width
			height = _height
			volPercent = 1
		End Method
		
		Method Draw()
			For Local i:Int = 0 To (dialNum*volPercent) - 1
				DrawRect(x + (i*width*2),y,width,height)
			Next
		End Method
		
		Method updateVol()
			If KeyHit(KEY_A) And volPercent > 0 Then
				volPercent -= 0.1
			End If
			If KeyHit(KEY_D) And volPercent < 1 Then
				volPercent += 0.1
			End If
		End Method
		
		Method getVol:Float()
			Return volPercent
		End
End Class


