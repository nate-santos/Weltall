#include "Support/BattlingFightDigest.h"

#include "Game/PsxRandom.h"

#include <cstring>
#include <span>
#include <string_view>

namespace Weltall::Testing
{
	namespace
	{
		class FBytes
		{
		public:
			template <typename... TValues>
			void U8(TValues... Values)
			{
				(Put<1>(static_cast<uint32>(Values)), ...);
			}

			template <typename... TValues>
			void I16(TValues... Values)
			{
				(Put<2>(static_cast<uint32>(Values)), ...);
			}

			template <typename... TValues>
			void I32(TValues... Values)
			{
				(Put<4>(static_cast<uint32>(Values)), ...);
			}

			void Point(const FBattlingPoint& Point) { I32(Point[0], Point[1], Point[2]); }
			void Shorts(const std::array<int16, 3>& Values) { I16(Values[0], Values[1], Values[2]); }

			void Matrix(const FGteMatrix& Matrix)
			{
				for (const auto& Row : Matrix.M)
				{
					I16(Row[0], Row[1], Row[2]);
				}
				I32(Matrix.T[0], Matrix.T[1], Matrix.T[2]);
			}

			void Text(std::string_view Text) { Out.insert(Out.end(), Text.begin(), Text.end()); }

			std::vector<uint8> Out;

		private:
			template <int32 Count>
			void Put(uint32 Value)
			{
				for (int32 Index = 0; Index < Count; ++Index)
				{
					Out.push_back(static_cast<uint8>(Value >> (8 * Index)));
				}
			}
		};

		uint8 Index8(int32 Value)
		{
			return Value < 0 ? static_cast<uint8>(0xFF) : static_cast<uint8>(Value);
		}

		void Fighter(FBytes& B, const FBattlingFighter& F, EBattlingRules Rules)
		{
			B.Point(F.Position);
			B.Point(F.Velocity);
			B.Point(F.KnockBack);
			B.I32(F.LungePush[0], F.LungePush[2], F.GroundSpeed, F.Lunge, F.Stick, F.Yaw, F.WantedDirection);
			B.Point(F.PreviousPosition);
			B.I32(F.Axes[0], F.Axes[1], F.Axes[2], F.Axes[3], F.Acceleration, F.Deceleration, F.GroundHeight, F.Flags, F.Flags2, F.Stun, F.Steer,
				F.HitRequest, F.AttackPower, F.BaseYaw, F.NearestDistance, F.Damage);
			B.Point(F.Source);
			B.Point(F.HitPoint);
			B.Point(F.Top);
			B.Point(F.Base);
			B.Point(F.Centre);
			B.Point(F.PreviousBase);
			B.I32(F.OverheatFromEther, F.OverheatFromDash, F.R1Presses, F.AttackPresses);
			B.U8(F.Animation, F.LastAnimation, F.AnimationSpeed, F.SpeedBonus, F.HpGauge, F.PreviousHpGauge, F.GaugeFlash, F.BackDashTimer, F.Movement,
				F.Action, F.LastMovement, F.Gear, F.Kind, F.SandDust, F.HeldAnimation, F.AlarmTimer, F.FarMoves, F.NearMoves, F.HitFlash, F.QueueWrite,
				F.QueueRead, F.QueueCount, F.Chain);
			B.I16(F.EventFrame, F.Hp, F.Heat, F.DrawnHp, F.OverheatDamage, F.MaxHp, F.EtherHeat, F.ReactionTimer, F.KnockDownTimer, F.CameraOffset,
				F.SteerOffset, F.HitsInRow, F.RoundsWon, F.AttackYaw, F.Cooldown, F.DownStun, F.FootHeights[0], F.FootHeights[1], F.FrameBefore,
				F.FramesStepped, F.Blend, F.Clock, F.MoveScale, F.SpeedScale, F.GearSpeedScale, F.HitsTaken);
			for (const uint8 Command : F.Queue)
			{
				B.U8(Command);
			}
			B.U8(F.Colour[0], F.Colour[1], F.Colour[2], Index8(F.MoveState), Index8(F.NearestProjectile), F.Record);
			const FBattlingReplayRecord& Record = F.Records[F.Record];
			B.I16(Record.X, Record.Y, Record.Z, Record.YawFlags);
			B.U8(Record.Animation, Record.Frames, Record.Blend, Record.bGuard ? 1 : 0);
			for (const FBattlingProjectile& P : F.Projectiles)
			{
				B.Point(P.Position);
				B.Point(P.Previous);
				B.Shorts(P.Velocity);
				B.Shorts(P.Direction);
				B.U8(P.bActive ? 1 : 0, P.Speed, P.Look, P.SpeedMode);
				B.I32(P.Distance, P.Id);
				B.I16(P.Power, P.Homing, P.Life);
			}
			for (const FBattlingHitVolume& V : F.Volumes)
			{
				B.Point(V.A);
				B.Point(V.PreviousA);
				B.Point(V.B);
				B.Point(V.PreviousB);
				B.U8(V.Joint, V.Flags, V.State, V.Kind, V.Power, V.Pass);
				B.I32(V.Id);
				B.U8(Index8(V.MoveState));
			}
			if (Rules != EBattlingRules::Original)
			{
				B.I16(F.RunScale);
				B.I32(F.DashHeat);
				B.U8(F.SwingHit);
			}
		}

		void Joints(FBytes& B, const FBattlingSkeleton& Skeleton)
		{
			B.Shorts(Skeleton.RootAngles);
			B.Matrix(Skeleton.RootWorld);
			// The model's list: a part the program hangs on a joint (El-Fenrir's) is not in it.
			for (const FBattlingJoint& Joint : std::span(Skeleton.Joints).first(Skeleton.File->Joints.size()))
			{
				B.Shorts(Joint.Angles);
				B.Shorts(Joint.Translation);
				B.U8(Joint.bHidden ? 1 : 0);
				B.Matrix(Joint.World);
			}
		}

		void Ai(FBytes& B, const FBattlingFight& Fight)
		{
			for (const FBattlingAiState& Ai : Fight.Ai)
			{
				B.I16(Ai.Timer);
				B.U8(static_cast<uint8>(Ai.Mode), Ai.Counter);
				B.I16(Ai.StickDirection, Ai.StickMagnitude);
				B.U8(Ai.bDash ? 1 : 0, Ai.Level);
				B.I32(Ai.Traits[0], Ai.Traits[1], Ai.Traits[2], Ai.Traits[3], Ai.Phase, Ai.HeatLimit, Ai.KeepDistance, Ai.Flags);
				B.I16(Ai.HitsAtRoll);
			}
		}

		uint8 MenuIndex(EBattlingMenu Menu)
		{
			return Menu == EBattlingMenu::None ? 0xFF : static_cast<uint8>(Menu);
		}

		void Menus(FBytes& B, const FBattlingFight& Fight)
		{
			const FBattlingMenus& Menus = Fight.Menus;
			B.U8(MenuIndex(Menus.Current), MenuIndex(Menus.Previous));
			for (const EBattlingMenu Menu : {EBattlingMenu::Pause, EBattlingMenu::GiveUp, EBattlingMenu::PracticePause})
			{
				B.I16(Menus.Cursors[static_cast<std::size_t>(Menu)]);
			}
			B.U8(Menus.HelpShown[0], Menus.HelpShown[1], Menus.HelpNext, Menus.bStickBuzzed ? 1 : 0, Menus.BlurPresses, Menus.bCaptureShown ? 1 : 0,
				Menus.bCaptureRestorePending ? 1 : 0, Fight.Settings.PracticeStyle, Fight.Settings.FrameSkip);
		}

		void FightGlobals(FBytes& B, const FBattlingFight& Fight, bool bRounds, bool bMenus)
		{
			B.U8(Fight.RecordIndex, Fight.SkipCounter, Fight.bFightStarted ? 1 : 0, Fight.Winner, Fight.MenuPad, Fight.CameraSide, Fight.CameraEase,
				Fight.State, Fight.Result);
			B.I32(Fight.IntroCounter, Fight.RoundTicks, Fight.SetTime, Fight.Round, Fight.bRoundOver ? 1 : 0, Fight.RoundOverTicks, Fight.Draws,
				Fight.Distance, Fight.DistanceXZ, Fight.FightAngle, Fight.CameraSideAngle);
			B.Point(Fight.Eye);
			B.Point(Fight.Target);
			B.I32(Fight.Effects.SparkRequest);
			B.Point(Fight.Effects.SparkSource);
			B.I32(Fight.VolumesSet, Fight.Contact[0], Fight.Contact[1], Fight.Random.Seed());
			for (int32 Call = 0; Call < Fight.VibrationCount; ++Call)
			{
				B.U8(Fight.Vibrations[static_cast<std::size_t>(Call)][0], Fight.Vibrations[static_cast<std::size_t>(Call)][1]);
			}
			if (!bRounds)
			{
				return;
			}
			B.I32(Fight.ReplayTicks);
			for (const FBattlingPoint& Point : {Fight.ReplayPositions[0], Fight.ReplayPositions[1], Fight.ReplayBases[0], Fight.ReplayBases[1]})
			{
				B.Point(Point);
			}
			B.I32(Fight.OrbitAngle, Fight.OrbitSpeed, Fight.CameraRadius, Fight.CameraLift, Fight.ResultAngle, Fight.ResultTargetHeight,
				Fight.ResultEyeHeight, Fight.ResultDistance);
			B.U8(Fight.WinnerSide);
			B.I32(Fight.WinnerText, Fight.WinnerPitch, Fight.WinnerSpin, Fight.WinnerCameraHeight, Fight.WinnerCameraDistance, Fight.WinnerScale);
			B.I16(Fight.WinnerYaw);
			B.U8(Fight.AnimationKinds[16].Kind, Fight.AnimationKinds[16].Next);
			if (bMenus)
			{
				Menus(B, Fight);
			}
		}

		void Effects(FBytes& B, const FBattlingEffects& Effects)
		{
			for (const FBattlingEffect& Slot : Effects.Slots)
			{
				B.U8(Slot.bActive ? 1 : 0, Slot.Frame, Slot.Count, static_cast<uint8>(Slot.Kind));
				B.Shorts(Slot.Position);
				if (!Slot.bActive)
				{
					continue;
				}
				switch (Slot.Kind)
				{
				case EBattlingEffectKind::Sprite:
					B.U8(Slot.Set, Slot.bTinted ? 1 : 0);
					B.I16(Slot.RiseAcceleration, Slot.RiseSpeed);
					if (Slot.bTinted)
					{
						B.U8(Slot.Colour[0], Slot.Colour[1], Slot.Colour[2]);
					}
					break;
				case EBattlingEffectKind::Trail:
					B.I16(Slot.Pass, Slot.Tick);
					B.I32(Slot.Id);
					B.I16(Slot.Length);
					B.U8(Index8(Slot.Link));
					break;
				case EBattlingEffectKind::Swept:
					B.I16(Slot.Pass, Slot.Tick);
					B.I32(Slot.Id);
					B.Shorts(Slot.End);
					B.U8(Index8(Slot.Link));
					break;
				}
			}
			B.U8(Effects.Colour[0], Effects.Colour[1], Effects.Colour[2]);
			B.I16(Effects.Tick);
			B.I32(Effects.LinesAdded, Effects.LinesShown);
			for (int32 Line = 0; Line < Effects.LinesAdded; ++Line)
			{
				const FBattlingLine& L = Effects.Lines[static_cast<std::size_t>(Line)];
				B.Shorts(L.From);
				B.Shorts(L.To);
				B.U8(L.Colour[0], L.Colour[1], L.Colour[2]);
			}
		}

		void Particles(FBytes& B, const FBattlingEffects& Effects)
		{
			for (const FBattlingDust& Dust : Effects.Dust)
			{
				B.Shorts(Dust.Position);
				B.U8(Dust.bLive ? 1 : 0, static_cast<uint8>(Dust.Speed));
				B.I16(Dust.Ground);
			}
			for (const FBattlingSplash& Splash : Effects.Splashes)
			{
				B.Shorts(Splash.Position);
				B.I16(Splash.Life);
				B.U8(static_cast<uint8>(Splash.SpeedX), static_cast<uint8>(Splash.SpeedZ));
				B.I16(Splash.SpeedY);
			}
			for (const FBattlingSpark& Spark : Effects.Sparks)
			{
				B.Shorts(Spark.Position);
				B.I16(Spark.Life);
				B.Shorts(Spark.Velocity);
			}
		}

		void Sound(FBytes& B, const FBattlingSoundChannels& Channels, const FBattlingSoundRecorder& Recorder)
		{
			for (const FBattlingSoundChannels::FChannel& Channel : Channels.Channels)
			{
				B.U8(static_cast<uint8>(Channel.Place), Channel.bActive ? 1 : 0, Channel.Voice, Channel.Tag);
				B.I16(Channel.Age);
				B.I32(Channel.Effect, Channel.VoiceMask);
				if (Channel.Place == EBattlingSoundPlace::Fixed)
				{
					B.Point(Channel.Point);
				}
			}
			B.Out.insert(B.Out.end(), Recorder.Calls.begin(), Recorder.Calls.end());
		}

		void Window(FBytes& B, const FMesWindow& Text, int16 Y)
		{
			const FMesWindow::FState State = Text.State();
			B.I16(State.X, State.Line, State.Flags, State.TopLine, State.LinesTyped);
			B.U8(State.BaseSpeed, State.Speed, State.SavedSpeed, State.Reason, State.bClosing ? 1 : 0);
			B.I16(State.Queued, State.WaitTimer, State.DelayCounter, State.DelayReload);
			B.U8(static_cast<uint32>(Text.HighlightedRow()));
			for (int32 Line = 0; Line < Text.Lines(); ++Line)
			{
				B.I16(State.Widths[static_cast<std::size_t>(Line)]);
			}
			B.U8(State.Next);
			B.I16(Text.Lines(), Y);
		}

		void Script(FBytes& B, const FBattlingFight& Fight)
		{
			constexpr int16 MenuY = 0x2A;
			const FBattlingTutorial& T = Fight.Tutorial;
			B.I16(T.Script);
			B.U8(Index8(T.Fighter));
			B.I32(T.Counter);
			B.I16(T.HeldHeat);
			B.I32(T.Message, T.MessageShown, static_cast<int32>(T.Camera), T.EndingStep);
			B.U8(T.bMenuOpen ? 1 : 0, T.MenuCursor, T.bPointerShown ? 1 : 0);
			B.I32(T.PointerGoal[0], T.PointerGoal[1], T.Pointer[0], T.Pointer[1]);
			for (const FBattlingTutorial::FSpark& Spark : T.Sparks)
			{
				B.Point(Spark.Point);
				B.I32(Spark.Timer);
			}
			B.I16(T.Fade);
			B.U8(Fight.bEndingActive ? 1 : 0);
			B.I32(Fight.TargetLift, Fight.CameraRadius, Fight.CameraLift);
			B.U8(Fight.AnimationKinds[10].Kind, Fight.AnimationKinds[10].Next);
			Window(B, T.Text, T.TextY);
			if (Fight.Settings.Mode == EBattlingMode::Tutorial)
			{
				Window(B, T.Menu, MenuY);
			}
		}

		void Fire(FBytes& B, const FBattlingFight& Fight)
		{
			B.I16(Fight.FireFade);
			B.U8(Fight.bFireActive ? 1 : 0);
			B.I32(Fight.Settings.MotionSpeed);
			B.U8(Fight.Fire.IsBurning() ? 1 : 0);
			if (Fight.Fire.IsBurning())
			{
				for (std::size_t Cell = 0; Cell < Fight.Fire.Picture().size(); ++Cell)
				{
					B.I16(Fight.Fire.Heat()[Cell]);
				}
				B.Out.insert(B.Out.end(), Fight.Fire.Picture().begin(), Fight.Fire.Picture().end());
			}
		}

		void Call(std::vector<uint8>& Calls, std::string_view Name, std::initializer_list<int32> Values)
		{
			Calls.insert(Calls.end(), Name.begin(), Name.end());
			for (const int32 Value : Values)
			{
				for (int32 Index = 0; Index < 4; ++Index)
				{
					Calls.push_back(static_cast<uint8>(static_cast<uint32>(Value) >> (8 * Index)));
				}
			}
		}
	}

	void FBattlingSoundRecorder::PlayEffect(uint32 Effect, uint8 Voice, int16 Volume, int16 Pan)
	{
		Call(Calls, "play", {static_cast<int32>(Effect), Voice, Volume, Pan});
	}

	void FBattlingSoundRecorder::SetPan(uint8 Voice, int16 Pan)
	{
		Call(Calls, "pan", {Voice, Pan});
	}

	void FBattlingSoundRecorder::SetVolume(uint8 Voice, int16 Volume)
	{
		Call(Calls, "volume", {Voice, Volume});
	}

	void FBattlingSoundRecorder::StopVoice(uint8 Voice)
	{
		Call(Calls, "stop", {Voice});
	}

	void FBattlingSoundRecorder::StopAll()
	{
		Call(Calls, "stop all", {});
	}

	void FBattlingSoundRecorder::StopSong()
	{
		Call(Calls, "stop song", {});
	}

	void FBattlingSoundRecorder::SetSongTempo(int32 Scale)
	{
		Call(Calls, "tempo", {Scale});
	}

	uint32 FBattlingSoundRecorder::PlayingVoices(uint32)
	{
		return 0;
	}

	std::vector<uint8> BattlingFightPartBytes(const FBattlingFight& Fight, EBattlingFightPart Part, const FBattlingSoundRecorder& Recorder, bool bRounds,
		bool bMenus)
	{
		FBytes B;
		switch (Part)
		{
		case EBattlingFightPart::Fighter1:
			Fighter(B, Fight.Fighters[0], Fight.Settings.Rules);
			break;
		case EBattlingFightPart::Fighter2:
			Fighter(B, Fight.Fighters[1], Fight.Settings.Rules);
			break;
		case EBattlingFightPart::Joints1:
			Joints(B, Fight.Fighters[0].Skeleton);
			break;
		case EBattlingFightPart::Joints2:
			Joints(B, Fight.Fighters[1].Skeleton);
			break;
		case EBattlingFightPart::Ai:
			Ai(B, Fight);
			break;
		case EBattlingFightPart::Fight:
			FightGlobals(B, Fight, bRounds, bMenus);
			break;
		case EBattlingFightPart::Effects:
			Effects(B, Fight.Effects);
			break;
		case EBattlingFightPart::Particles:
			Particles(B, Fight.Effects);
			break;
		case EBattlingFightPart::Sound:
			Sound(B, Fight.Sound, Recorder);
			break;
		case EBattlingFightPart::Script:
			Script(B, Fight);
			break;
		case EBattlingFightPart::Fire:
			Fire(B, Fight);
			break;
		}
		return std::move(B.Out);
	}

	std::vector<uint8> BattlingArenaMenusBytes(const FBattlingFight& Fight, const FBattlingSoundRecorder& Recorder)
	{
		FBytes B;
		const FBattlingMenus& Menus = Fight.Menus;
		const FBattlingGearChoice& Gears = Menus.Gears;
		const FBattlingSettings& Settings = Fight.Settings;
		B.U8(MenuIndex(Menus.Current), MenuIndex(Menus.Previous));
		for (std::size_t Menu = 0; Menu < Menus.Cursors.size(); ++Menu)
		{
			B.I16(Menus.Cursors[Menu]);
			B.U8(MenuIndex(Menus.Backs[Menu]));
		}
		for (const EBattlingMenu Menu : {EBattlingMenu::PlayerOneGear, EBattlingMenu::PlayerTwoGear})
		{
			B.U8(Menus.ItemHelp(Menu, 0), Menus.ItemFlags(Menu, 1));
		}
		B.U8(Menus.HelpShown[0], Menus.HelpShown[1], Menus.HelpNext, Menus.bStickBuzzed ? 1 : 0, Menus.BlurPresses, Menus.bCaptureShown ? 1 : 0);
		B.U8(Menus.InputPad);
		B.I16(Menus.InputHeld, Menus.InputPressed, Menus.InputRepeated);
		B.I32(Gears.Cursors[0], Gears.Cursors[1], Gears.Chosen, Gears.Shown[0], Gears.Shown[1], Gears.NearSlides[0], Gears.NearSlides[1],
			Gears.FarSlides[0], Gears.FarSlides[1]);
		B.U8(Gears.NameFlashes[0] ? 1 : 0, Gears.NameFlashes[1] ? 1 : 0, Gears.bSecondChoosing ? 1 : 0, Gears.bPortraitsUploaded ? 1 : 0);
		B.U8(Index8(Menus.GearsRead[0]), Index8(Menus.GearsRead[1]));
		B.U8(Settings.Level, Settings.Vibration[0] ? 1 : 0, Settings.Vibration[1] ? 1 : 0, Settings.Computer[0] ? 1 : 0, Settings.Computer[1] ? 1 : 0,
			Settings.WinsPerSet, Settings.bRubberBand ? 1 : 0, Settings.PracticeStyle, Settings.FrameSkip, static_cast<uint8>(Settings.Mode), Fight.State,
			Menus.bBoutChosen ? 1 : 0);
		B.I32(Menus.IdleCountdown, Menus.L2HeldPasses, Fight.Random.Seed());
		Fire(B, Fight);
		B.Out.insert(B.Out.end(), Recorder.Calls.begin(), Recorder.Calls.end());
		return std::move(B.Out);
	}
}
