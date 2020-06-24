Shader "Unlit/YUV2RGBA"
{
	Properties
	{
		_MainTex ("Texture", 2D) = "black" {}
		_YTex("Y channel", 2D) = "black" {}
		_UTex("U channel", 2D) = "gray" {}
		_VTex("V channel", 2D) = "gray" {}
	}
	SubShader
	{
		Tags { "RenderType"="Opaque" }
		LOD 100

		Pass
		{
			CGPROGRAM
			#pragma vertex vert
			#pragma fragment frag
			
			#include "UnityCG.cginc"

			struct appdata
			{
				float4 vertex : POSITION;
				float2 uv : TEXCOORD0;
			};

			struct v2f
			{
				float2 uv : TEXCOORD0;
				float4 vertex : SV_POSITION;
			};

			sampler2D _MainTex;
			sampler2D _YTex;
			sampler2D _UTex;
			sampler2D _VTex;
			
			v2f vert (appdata v)
			{
				v2f o;
				o.vertex = UnityObjectToClipPos(v.vertex);
				o.uv = float2(1-v.uv.x, v.uv.y);
				return o;
			}
			
			fixed4 frag (v2f i) : SV_Target
			{
				float ych = tex2D(_YTex, i.uv).a;
				float uch = tex2D(_UTex, i.uv).a - 0.5;
				float vch = tex2D(_VTex, i.uv).a - 0.5;

				fixed4 col;
				col.r = ych + 1.4 * vch;
				col.g = ych - 0.343 * uch - 0.711 * vch;
				col.b = ych + 1.765 * uch;
				col.a = 1;

				col = clamp(col, 0.0, 1.0);

				if(!IsGammaSpace()) {	//	If linear space.
					col = pow(col, 2.2);
				}

				return col;
			}
			ENDCG
		}
	}
}
