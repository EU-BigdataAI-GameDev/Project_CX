// Copyright Epic Games, Inc. All Rights Reserved.

#include "SisterCharacter.h"
#include "SisterAnimInstance.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"
#include "Engine/LocalPlayer.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputActionValue.h"

ASisterCharacter::ASisterCharacter()
{
	// 기획 키 160cm(오빠 180cm). 원본 Sister_SK가 약 118cm라 메시를 1.355배 균일 스케일한다.
	GetCapsuleComponent()->InitCapsuleSize(34.0f, 80.0f);

	GetMesh()->SetRelativeLocationAndRotation(FVector(0.0f, 0.0f, -80.0f), FRotator(0.0f, -90.0f, 0.0f));
	GetMesh()->SetRelativeScale3D(FVector(1.355f));
	GetMesh()->SetAnimationMode(EAnimationMode::AnimationBlueprint);
	GetMesh()->AnimClass = USisterAnimInstance::StaticClass();

	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;

	// 이동 수치는 BP_Brother와 동일하게 맞춰 두 캐릭터의 조작감을 통일한다.
	UCharacterMovementComponent* Movement = GetCharacterMovement();
	Movement->bOrientRotationToMovement = true;
	Movement->RotationRate = FRotator(0.0f, 500.0f, 0.0f);
	Movement->MaxWalkSpeed = 450.0f;
	Movement->MinAnalogWalkSpeed = 20.0f;
	Movement->JumpZVelocity = 420.0f;
	Movement->AirControl = 0.35f;
	Movement->BrakingDecelerationWalking = 2000.0f;

	// 이동이 월드 절대축 기준이므로 카메라도 월드 기준으로 고정한다(캐릭터가 돌아도 화면은 돌지 않음).
	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(RootComponent);
	CameraBoom->SetUsingAbsoluteRotation(true);
	CameraBoom->SetRelativeRotation(FRotator(-10.0f, 0.0f, 0.0f));
	CameraBoom->TargetArmLength = 800.0f;
	CameraBoom->SocketOffset = FVector(0.0f, 0.0f, 100.0f);
	CameraBoom->bUsePawnControlRotation = false;

	FollowCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FollowCamera"));
	FollowCamera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
	FollowCamera->bUsePawnControlRotation = false;
}

void ASisterCharacter::NotifyControllerChanged()
{
	Super::NotifyControllerChanged();

	// 로컬 2인 플레이: 각 로컬 플레이어가 자신의 Enhanced Input 서브시스템을 가지므로,
	// 이 캐릭터를 소유한 플레이어(2P)의 서브시스템에만 컨텍스트를 추가한다.
	if (const APlayerController* PlayerController = Cast<APlayerController>(Controller))
	{
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PlayerController->GetLocalPlayer()))
		{
			if (DefaultMappingContext)
			{
				Subsystem->AddMappingContext(DefaultMappingContext, 0);
			}
		}
	}
}

void ASisterCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	if (UEnhancedInputComponent* EnhancedInputComponent = Cast<UEnhancedInputComponent>(PlayerInputComponent))
	{
		EnhancedInputComponent->BindAction(JumpAction, ETriggerEvent::Started, this, &ACharacter::Jump);
		EnhancedInputComponent->BindAction(JumpAction, ETriggerEvent::Completed, this, &ACharacter::StopJumping);
		EnhancedInputComponent->BindAction(MoveAction, ETriggerEvent::Triggered, this, &ASisterCharacter::Move);
	}
}

void ASisterCharacter::Move(const FInputActionValue& Value)
{
	// AProject_CXCharacter::DoMove와 동일하게 월드 절대축 기준(X=전진, Y=우측)으로 이동한다.
	const FVector2D Input = Value.Get<FVector2D>();
	AddMovementInput(FVector::ForwardVector, Input.Y);
	AddMovementInput(FVector::RightVector, Input.X);
}
